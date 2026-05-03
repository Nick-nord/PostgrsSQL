// PostgrsSQL.cpp : Этот файл содержит функцию "main". Здесь начинается и заканчивается выполнение программы.
//

#include <iostream>
#include <string>
#include <vector>
#include <pqxx/pqxx>
#include <stdexcept>

// Структура для хранения информации о клиенте
struct Client {
    int id;
    std::string first_name;
    std::string last_name;
    std::string email;
    std::vector<std::string> phones;
};

class ClientManager {
private:
    std::string connection_string;

public:
    // Конструктор принимает строку подключения к БД
    ClientManager(const std::string& conn_str) : connection_string(conn_str) {
        createDB();
    }

private:
    // Метод для создания таблиц, если они не существуют
    void createDB() {
        pqxx::connection conn(connection_string);
        if (!conn.is_open()) {
            throw std::runtime_error("Не удалось подключиться к базе данных");
        }
        pqxx::work txn(conn);
        txn.exec(R"(
            CREATE TABLE IF NOT EXISTS clients (
                client_id SERIAL PRIMARY KEY,
                first_name VARCHAR(100) NOT NULL,
                last_name VARCHAR(100) NOT NULL,
                email VARCHAR(255) UNIQUE NOT NULL
            );
            CREATE TABLE IF NOT EXISTS phones (
                phone_id SERIAL PRIMARY KEY,
                client_id INTEGER NOT NULL,
                phone_number VARCHAR(20) NOT NULL,
                FOREIGN KEY (client_id) REFERENCES clients(client_id) ON DELETE CASCADE
            );
        )");
        txn.commit();
        conn.disconnect();
    }

public:
    // 1. Добавить нового клиента
    int addClient(const std::string& first_name, const std::string& last_name, const std::string& email) {
        pqxx::connection conn(connection_string);
        pqxx::work txn(conn);
        pqxx::result result = txn.prepared("add_client")
            (first_name)(last_name)(email).exec();
        int new_client_id = result[0][0].as<int>();
        txn.commit();
        return new_client_id;
    }

    // 2. Добавить телефон для существующего клиента
    void addPhone(int client_id, const std::string& phone_number) {
        pqxx::connection conn(connection_string);
        pqxx::work txn(conn);
        txn.prepared("add_phone")(client_id)(phone_number).exec();
        txn.commit();
    }

    // 3. Изменить данные о клиенте
    void updateClient(int client_id, const std::string& first_name, const std::string& last_name, const std::string& email) {
        pqxx::connection conn(connection_string);
        pqxx::work txn(conn);
        txn.prepared("update_client")(first_name)(last_name)(email)(client_id).exec();
        txn.commit();
    }

    // 4. Удалить телефон у существующего клиента
    void deletePhone(int phone_id) {
        pqxx::connection conn(connection_string);
        pqxx::work txn(conn);
        txn.prepared("delete_phone")(phone_id).exec();
        txn.commit();
    }

    // 5. Удалить существующего клиента (и все его телефоны)
    void deleteClient(int client_id) {
        pqxx::connection conn(connection_string);
        pqxx::work txn(conn);
        txn.prepared("delete_client")(client_id).exec();
        txn.commit();
    }

    // 6. Найти клиента по его данным (имя, фамилия, email или телефон)
    std::vector<Client> findClients(const std::string& query_value) {
        pqxx::connection conn(connection_string);

        // Подготавливаем запрос для поиска. Используем ILIKE для регистронезависимого поиска.
        pqxx::nontransaction ntxn(conn);

        // Сначала получаем всех клиентов, у которых совпало поле в основной таблице.
        pqxx::result client_results = ntxn.prepared("find_clients_by_main_fields")
            (query_value)(query_value)(query_value).exec();

        std::vector<Client> found_clients;
        for (const auto& row : client_results) {
            Client c{
                row["client_id"].as<int>(),
                row["first_name"].as<std::string>(),
                row["last_name"].as<std::string>(),
                row["email"].as<std::string>(),
                {}
            };
            found_clients.push_back(c);
        }

        // Теперь ищем клиентов по номеру телефона и добавляем их, если они еще не в списке.
        pqxx::result phone_results = ntxn.prepared("find_clients_by_phone")
            (query_value).exec();

        for (const auto& row : phone_results) {
            Client c{
                row["client_id"].as<int>(),
                row["first_name"].as<std::string>(),
                row["last_name"].as<std::string>(),
                row["email"].as<std::string>(),
                {row["phone_number"].as<std::string>()}
            };

            // Проверяем, не добавили ли мы этого клиента ранее по ФИО/email
            bool already_exists = false;
            for (auto& existing : found_clients) {
                if (existing.id == c.id) {
                    existing.phones.push_back(c.phones[0]);
                    already_exists = true;
                    break;
                }
            }
            if (!already_exists) {
                found_clients.push_back(c);
            }
        }

        return found_clients;
    }
};

int main() {
    try {
        // Строка подключения к PostgreSQL.
        // Формат: "host=127.0.0.1 user=postgres password=your_password dbname=client_db"
        std::string conn_str = "host=127.0.0.1 user=postgres dbname=client_db";
        ClientManager cm(conn_str);

        // --- Инициализация подготовленных запросов ---
        pqxx::connection setup_conn(conn_str);
        pqxx::work setup_txn(setup_conn);

        setup_txn.prepare("add_client",
            "INSERT INTO clients(first_name, last_name, email) VALUES ($1, $2, $3) RETURNING client_id");

        setup_txn.prepare("add_phone",
            "INSERT INTO phones(client_id, phone_number) VALUES ($1, $2)");

        setup_txn.prepare("update_client",
            "UPDATE clients SET first_name=$1, last_name=$2, email=$3 WHERE client_id=$4");

        setup_txn.prepare("delete_phone",
            "DELETE FROM phones WHERE phone_id=$1");

        setup_txn.prepare("delete_client",
            "DELETE FROM clients WHERE client_id=$1");

        setup_txn.prepare("find_clients_by_main_fields",
            "SELECT * FROM clients WHERE first_name ILIKE $1 OR last_name ILIKE $2 OR email ILIKE $3");

        setup_txn.prepare("find_clients_by_phone",
            "SELECT c.client_id, c.first_name, c.last_name, c.email, p.phone_number "
            "FROM clients c JOIN phones p ON c.client_id = p.client_id "
            "WHERE p.phone_number ILIKE $1");

        setup_txn.commit();
        setup_conn.disconnect();

        // --- Пример использования ---
        int new_client_id = cm.addClient("Иван", "Иванов", "ivanov@example.com");
        cm.addPhone(new_client_id, "+79991112233");
        cm.addPhone(new_client_id, "+79994445566");

        std::cout << "Добавлен клиент с ID: " << new_client_id << "\n";

        std::vector<Client> results = cm.findClients("ivanov@example.com");
        for (const Client& c : results) {
            std::cout << "Найден клиент: " << c.first_name << " " << c.last_name << ", Email: " << c.email << "\n";
            for (const std::string& phone : c.phones) {
                std::cout << "   Телефон: " << phone << "\n";
            }
        }

    }
    catch (const std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << "\n";
    }
    return 0;
}
// Запуск программы: CTRL+F5 или меню "Отладка" > "Запуск без отладки"
// Отладка программы: F5 или меню "Отладка" > "Запустить отладку"

// Советы по началу работы 
//   1. В окне обозревателя решений можно добавлять файлы и управлять ими.
//   2. В окне Team Explorer можно подключиться к системе управления версиями.
//   3. В окне "Выходные данные" можно просматривать выходные данные сборки и другие сообщения.
//   4. В окне "Список ошибок" можно просматривать ошибки.
//   5. Последовательно выберите пункты меню "Проект" > "Добавить новый элемент", чтобы создать файлы кода, или "Проект" > "Добавить существующий элемент", чтобы добавить в проект существующие файлы кода.
//   6. Чтобы снова открыть этот проект позже, выберите пункты меню "Файл" > "Открыть" > "Проект" и выберите SLN-файл.
