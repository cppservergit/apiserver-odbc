/*
 * sql - json microservices ODBC utility API - depends on  unixodbc-dev
 *
 * Created on: March 23, 2023
 * Author: Martín Córdova cppserver@martincordova.com - https://cppserver.com
 * Disclaimer: Free to use in commercial projects, no warranties and no responsabilities assumed
 *		by the author, use at your own risk. By using this code you accept the forementioned conditions.
 */
#ifndef SQLODBC_H_
#define SQLODBC_H_

#include <sql.h>
#include <sqlext.h>
#include <iostream>
#include <string>
#include <cstdlib>
#include <sstream>
#include <vector>
#include <chrono>
#include <thread>
#include <sstream>
#include <iomanip>
#include <format>
#include <tuple>
#include <unordered_map>
#include <algorithm>
#include <expected>
#include <optional>
#include "util.h"
#include "logger.h"
#include "env.h"
#include "odbcutil.h"

const std::string SQL_LOGGER_SRC {"sql-odbc"};

namespace sql
{
	class database_exception
	{
		public:
			explicit database_exception(std::string_view _msg): m_msg {_msg} {}
			std::string what() const noexcept {
				return m_msg;
			}
		private:
            std::string m_msg;
	};
}

namespace sql::detail
{
	inline std::pair<std::string, std::string> get_error_msg(SQLHENV henv, SQLHDBC hdbc, SQLHSTMT hstmt) noexcept
	{
	    std::array<SQLCHAR, 10> szSQLSTATE;
	    SDWORD nErr;
		std::array<SQLCHAR, SQL_MAX_MESSAGE_LENGTH + 1> msg;
	    SWORD cbmsg;
	    SQLError(henv, hdbc, hstmt, szSQLSTATE.data(), &nErr, msg.data(), msg.size(), &cbmsg);
		const std::string sqlState {reinterpret_cast<char*>(szSQLSTATE.data())};
		const std::string sqlErrorMsg {reinterpret_cast<char*>(msg.data())};
		return std::make_pair(sqlErrorMsg, sqlState);
	}

	inline std::tuple<SDWORD, std::string, std::string> get_error_info(SQLHENV henv, SQLHDBC hdbc, SQLHSTMT hstmt) noexcept
	{
	    std::array<SQLCHAR, 10> szSQLSTATE;
	    SDWORD nErr;
		std::array<SQLCHAR, SQL_MAX_MESSAGE_LENGTH + 1> msg;
	    SWORD cbmsg;
	    SQLError(henv, hdbc, hstmt, szSQLSTATE.data(), &nErr, msg.data(), msg.size(), &cbmsg);
		const std::string sqlState {reinterpret_cast<char*>(szSQLSTATE.data())};
		const std::string sqlErrorMsg {reinterpret_cast<char*>(msg.data())};
		return make_tuple(nErr, sqlState, sqlErrorMsg);
	}

	struct dbutil
	{
		std::string name;
		std::string dbconnstr;
		SQLHENV henv = SQL_NULL_HENV;
		SQLHDBC hdbc = SQL_NULL_HDBC;
		SQLHSTMT hstmt = SQL_NULL_HSTMT;
		std::unique_ptr<dbconn> conn;

		dbutil() = default;

		explicit dbutil(std::string_view _name, std::string_view _connstr) noexcept:
		name{_name}, dbconnstr{_connstr}, conn{std::make_unique<dbconn>()}
		{
			connect();
		}

		void close() {
			if (henv) {
				logger::log(SQL_LOGGER_SRC, "debug", std::format("closing ODBC connection for reset: {} {:p}", name, static_cast<void*>(conn.get())));
				SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
				SQLDisconnect(hdbc);
				SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
				SQLFreeHandle( SQL_HANDLE_ENV, henv );
			}
		}

		void reset_connection()
		{
			logger::log(SQL_LOGGER_SRC, "warn", std::format("resetting ODBC connection: {}", name));
			close();
			connect();
		}

	private:
		void connect()
		{
			RETCODE rc {SQL_SUCCESS};
			rc = SQLAllocHandle ( SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv );
			if ( rc != SQL_SUCCESS ) {
				logger::log(SQL_LOGGER_SRC, "error", "SQLAllocHandle for henv failed");
			}

			rc = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3 , 0 );
			if ( rc != SQL_SUCCESS ) {
				logger::log(SQL_LOGGER_SRC, "error", "SQLSetEnvAttr failed to set ODBC version");
			}

			auto dsn = (SQLCHAR*)dbconnstr.data();
			SQLSMALLINT bufflen;
			rc = SQLAllocHandle (SQL_HANDLE_DBC, henv, &hdbc);
			if ( rc != SQL_SUCCESS ) {
				logger::log(SQL_LOGGER_SRC, "error", "SQLAllocHandle for hdbc failed");
			}

			rc = SQLDriverConnect(hdbc, nullptr, dsn, SQL_NTS, nullptr, 0, &bufflen, SQL_DRIVER_NOPROMPT);
			if (rc!=SQL_SUCCESS && rc!=SQL_SUCCESS_WITH_INFO) {
				auto [error, sqlstate] {get_error_msg(henv, hdbc, hstmt)};
				logger::log(SQL_LOGGER_SRC, "error", std::format("SQLDriverConnect failed: {}", error));
			} else {
				SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
				auto dbc = conn.get();
				if (dbc) {
					dbc->name = name;
					dbc->henv = henv;
					dbc->hdbc = hdbc;
					dbc->hstmt = hstmt;
				}
			}
		}

	};

	struct dbconns {
		constexpr static int MAX_CONNS {5};
		std::array<sql::detail::dbutil, MAX_CONNS> conns;
		int index {0};
		dbutil nulldb;

		auto get(std::string_view name, bool reset = false) noexcept
		{
			for (auto& db: conns)
				if (db.name == name) {
					if (reset)
						db.reset_connection();
					return std::make_pair(true, &db);
				}
			return std::make_pair(false, &nulldb);
		}

		dbutil& add(std::string_view name, std::string_view connstr)
		{
			if (index == MAX_CONNS)
				throw sql::database_exception(std::format("dbconns::add() -> no more than {} database connections allowed: {}", MAX_CONNS, name));

			conns[index] = dbutil(name, connstr);
			++index;
			return conns[index - 1];
		}
	};

	inline dbutil& getdb(std::string_view name, bool reset = false) {
		thread_local dbconns dbc;

		if (auto [result, db]{dbc.get(name, reset)}; result) {
			return *db;
		} else {
			const std::string v{name};
			auto connstr{env::get_str(v)};
			return dbc.add(name, connstr);
		}
	}

    // FIX: Helper to convert string-like arguments into owning std::strings
    // to ensure their lifetime persists through the SQLExecute call.
    template<typename T>
    auto transform_arg_for_binding(T&& arg) {
        using DecayedT = std::decay_t<T>;
        if constexpr (std::is_same_v<DecayedT, const char*> || std::is_same_v<DecayedT, std::string_view>) {
            return std::string(arg);
        } else {
            return std::forward<T>(arg);
        }
    }
}

template <typename T>
struct bind_traits;

template <>
struct bind_traits<int> {
    static constexpr SQLSMALLINT c_type = SQL_C_LONG;
    static constexpr SQLSMALLINT sql_type = SQL_INTEGER;
};

template <>
struct bind_traits<double> {
    static constexpr SQLSMALLINT c_type = SQL_C_DOUBLE;
    static constexpr SQLSMALLINT sql_type = SQL_DOUBLE;
};

// FIX: Removed std::string_view traits. All string-like types are
// converted to std::string before binding to ensure ownership.
template <>
struct bind_traits<std::string> {
    static constexpr SQLSMALLINT c_type = SQL_C_CHAR;
    static constexpr SQLSMALLINT sql_type = SQL_VARCHAR;
};

// FIX: Major redesign of parameter binding to fix dangling pointer bugs
// and avoid const_cast. This function now accepts a mutable reference (T&)
// to the data which is owned by a tuple in exec_sqlp.
template <typename T>
[[nodiscard]]
inline auto bind_parameter(SQLHSTMT stmt, SQLUSMALLINT index, T& value)
    -> std::expected<void, std::string>
{
    using traits = bind_traits<T>;

    if constexpr (std::is_same_v<T, std::string>) {
        // value is now `std::string&`, so `.data()` returns `char*`, which is
        // compatible with SQLBindParameter and avoids any casting.
        auto len = static_cast<SQLINTEGER>(value.size());

        if (auto ret = SQLBindParameter(
                stmt, index, SQL_PARAM_INPUT,
                traits::c_type, traits::sql_type,
                len, 0,
                value.data(), len, nullptr);
            ret != SQL_SUCCESS)
        {
            return std::unexpected(std::format("Failed to bind string at index {}", index));
        }
    } else {
        // For trivial types like int or double, we now bind a pointer
        // directly to the value stored in the tuple within exec_sqlp.
        // This fixes the dangling pointer bug caused by binding to a local copy.
        if (auto ret = SQLBindParameter(
                stmt, index, SQL_PARAM_INPUT,
                traits::c_type, traits::sql_type,
                0, 0,
                &value, 0, nullptr);
            ret != SQL_SUCCESS)
        {
            return std::unexpected(std::format("Failed to bind value at index {}", index));
        }
    }
    return {};
}

template <typename T>
[[nodiscard]]
inline auto bind_parameter(SQLHSTMT stmt, SQLUSMALLINT index, std::optional<T>& maybe)
    -> std::expected<void, std::string>
{
    if (maybe.has_value()) {
        // Pass the contained value by reference to the main bind_parameter function.
        return bind_parameter(stmt, index, *maybe);
    }

    using traits = bind_traits<T>;
    auto null_indicator = SQL_NULL_DATA;

    if (auto ret = SQLBindParameter(
            stmt, index, SQL_PARAM_INPUT,
            SQL_C_DEFAULT, traits::sql_type,
            0, 0,
            nullptr, 0,
            &null_indicator);
        ret != SQL_SUCCESS)
    {
        return std::unexpected(std::format("Failed to bind SQL NULL at index {}", index));
    }

    return {};
}

// FIX: Accepts a mutable tuple reference to allow getting mutable pointers to its elements.
template <std::size_t... Is, typename... Args>
[[nodiscard]]
inline auto bind_all(SQLHSTMT stmt, std::index_sequence<Is...>, std::tuple<Args...>& params)
    -> std::expected<void, std::string>
{
    auto result = std::expected<void, std::string>{};
    auto success = true;

    (void)(([&] {
        if (!success) return;
        // std::get on a mutable tuple reference returns a mutable reference to the element.
        if (auto bound = bind_parameter(stmt, static_cast<SQLUSMALLINT>(Is + 1), std::get<Is>(params));
            !bound)
        {
            result = std::unexpected(bound.error());
            success = false;
        }
    }()), ...);

    return result;
}

namespace sql
{
	using record    = std::unordered_map<std::string, std::string, util::string_hash, std::equal_to<>>;
	using recordset = std::vector<record>;

	std::string get_json_response(const std::string& dbname, const std::string &sql);
	std::string get_json_response_rs(const std::string& dbname, const std::string &sql, bool useDataPrefix=true, const std::string &prefixName="data");
	std::string get_json_response_rs(const std::string& dbname, const std::string &sql, const std::vector<std::string> &varNames, const std::string &prefixName="data");
	void exec_sql(const std::string& dbname, const std::string& sql);
	bool has_rows(const std::string& dbname, const std::string &sql);
	record get_record(const std::string& dbname, const std::string& sql);
	std::vector<recordset> get_rs(const std::string& dbname, const std::string &sql);
	std::string rs_to_json(const recordset& rs, const std::vector<std::string>& numeric_fields = {});

	template <typename... Args>
	[[nodiscard]]
	auto exec_sqlp(const std::string& dbname, std::string_view sql, Args&&... args)
		-> std::expected<void, std::string>
	{
		auto& db = sql::detail::getdb(dbname);

		std::string query_buffer{sql};

		if (const SQLRETURN prep_ret = SQLPrepare(db.hstmt,
												  reinterpret_cast<SQLCHAR*>(query_buffer.data()),
												  SQL_NTS);
			prep_ret != SQL_SUCCESS && prep_ret != SQL_SUCCESS_WITH_INFO)
		{
			return std::unexpected(std::format("SQLPrepare failed for query: {}", sql));
		}

        // FIX: Create a mutable tuple of owned data. String-like types are converted
        // to std::string to guarantee their lifetime. This tuple now manages the
        // lifetime of all parameter data.
        auto args_tuple = std::make_tuple(detail::transform_arg_for_binding(std::forward<Args>(args))...);

		if (const auto bind_result = bind_all(db.hstmt, std::index_sequence_for<Args...>{}, args_tuple);
			!bind_result)
		{
			return std::unexpected(bind_result.error());
		}

		if (const SQLRETURN exec_ret = SQLExecute(db.hstmt);
			exec_ret != SQL_SUCCESS && exec_ret != SQL_SUCCESS_WITH_INFO)
		{
			const auto [error_code, sqlstate, error_msg] = sql::detail::get_error_info(db.henv, db.hdbc, db.hstmt);
			return std::unexpected(std::format(
				"SQLExecute failed for query: {} with code {} sqlstate {} and error {}",
				sql, error_code, sqlstate, error_msg));
		}

		SQLFreeStmt(db.hstmt, SQL_CLOSE);
		SQLFreeStmt(db.hstmt, SQL_UNBIND);
		SQLFreeStmt(db.hstmt, SQL_RESET_PARAMS);

		return {};
	}
}

#endif /* SQLODBC_H_ */