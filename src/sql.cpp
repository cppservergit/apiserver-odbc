#include "sql.h"

namespace 
{
	constexpr int max_retries {10};
	const std::string LOGGER_SRC {"sql-odbc"};
	
	std::pair<std::string, std::string> get_error_msg(SQLHENV henv, SQLHDBC hdbc, SQLHSTMT hstmt) noexcept
	{
	    std::array<SQLCHAR, 10> szSQLSTATE;
	    SDWORD nErr;
		std::array<SQLCHAR, SQL_MAX_MESSAGE_LENGTH + 1> msg;
	    SWORD cbmsg;
	    SQLError(henv, hdbc, hstmt, szSQLSTATE.data(), &nErr, msg.data(), msg.size(), &cbmsg);
		const std::string sqlState {std::bit_cast<char*>(szSQLSTATE.data())};
		const std::string sqlErrorMsg {std::bit_cast<char*>(msg.data())};
		return std::make_pair(sqlErrorMsg, sqlState);
	}

	std::tuple<SDWORD, std::string, std::string> get_error_info(SQLHENV henv, SQLHDBC hdbc, SQLHSTMT hstmt) noexcept
	{
	    std::array<SQLCHAR, 10> szSQLSTATE;
	    SDWORD nErr;
		std::array<SQLCHAR, SQL_MAX_MESSAGE_LENGTH + 1> msg;
	    SWORD cbmsg;
	    SQLError(henv, hdbc, hstmt, szSQLSTATE.data(), &nErr, msg.data(), msg.size(), &cbmsg);
		const std::string sqlState {std::bit_cast<char*>(szSQLSTATE.data())};
		const std::string sqlErrorMsg {std::bit_cast<char*>(msg.data())};
		return make_tuple(nErr, sqlState, sqlErrorMsg);
	}

	struct col_info {
		std::string colname;
		SQLSMALLINT dataType{0};
		SQLLEN dataBufferSize{0};
		SQLLEN dataSize{0};
		std::vector<SQLCHAR> data;

		col_info(const std::string& _colname, const SQLSMALLINT _dataType, const SQLLEN _dataBufferSize ):
			colname{_colname},
			dataType{_dataType},
			dataBufferSize{_dataBufferSize}
		{
			data.resize(dataBufferSize);
		};
	};

	inline std::vector<col_info> bind_cols(const SQLHSTMT& hstmt, const SQLSMALLINT& numCols) {
		
		std::vector<col_info> cols;
		cols.reserve( numCols );

		for (int i = 0; i < numCols; i++) {
			std::array<SQLCHAR, 50> colname;
			SQLSMALLINT NameLength{0}; 
			SQLSMALLINT dataType{0};
			SQLSMALLINT DecimalDigits{0};
			SQLSMALLINT Nullable{0};
			SQLULEN ColumnSize{0};
			SQLLEN displaySize{0};
			SQLDescribeCol(hstmt, i + 1, colname.data(), colname.size(), &NameLength, &dataType, &ColumnSize, &DecimalDigits, &Nullable);
			SQLColAttribute(hstmt, i + 1, SQL_DESC_DISPLAY_SIZE, nullptr, 0, nullptr, &displaySize);
			displaySize++;
			col_info& col = cols.emplace_back(std::bit_cast<char*>(colname.data()), dataType, displaySize);
			SQLBindCol(hstmt, i + 1, SQL_C_CHAR, &col.data[0], col.dataBufferSize, &col.dataSize);
		}
		
		return cols;
	}

	struct dbutil 
	{
		std::string name;
		std::string dbconnstr;
		SQLHENV henv = SQL_NULL_HENV;
		SQLHDBC hdbc = SQL_NULL_HDBC;
		SQLHSTMT hstmt = SQL_NULL_HSTMT;
		std::unique_ptr<dbconn> conn;
				
		dbutil(): conn{nullptr} {}
	
		explicit dbutil(std::string_view _name, std::string_view _connstr) noexcept: 
		name{_name}, dbconnstr{_connstr}, conn{std::make_unique<dbconn>()}
		{
			connect();
		}
		
		constexpr void close() {
			if (henv) {
				logger::log(LOGGER_SRC, "debug", std::format("closing ODBC connection for reset: {} {:p}", name, static_cast<void*>(conn.get())));
				SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
				SQLDisconnect(hdbc);
				SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
				SQLFreeHandle( SQL_HANDLE_ENV, henv );
			}
		}
	
		constexpr void reset_connection()
		{
			logger::log(LOGGER_SRC, "warn", std::format("resetting ODBC connection: {}", name));
			close();
			connect();
		}
		
	private:
		constexpr void connect()
		{
			RETCODE rc {SQL_SUCCESS};
			rc = SQLAllocHandle ( SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv );
			if ( rc != SQL_SUCCESS ) {
				logger::log(LOGGER_SRC, "error", "SQLAllocHandle for henv failed");
			}

			rc = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3 , 0 );
			if ( rc != SQL_SUCCESS ) {
				logger::log(LOGGER_SRC, "error", "SQLSetEnvAttr failed to set ODBC version");
			}

			auto dsn = (SQLCHAR*)dbconnstr.data();
			SQLSMALLINT bufflen;
			rc = SQLAllocHandle (SQL_HANDLE_DBC, henv, &hdbc);
			if ( rc != SQL_SUCCESS ) {
				logger::log(LOGGER_SRC, "error", "SQLAllocHandle for hdbc failed");
			}
		
			rc = SQLDriverConnect(hdbc, nullptr, dsn, SQL_NTS, nullptr, 0, &bufflen, SQL_DRIVER_NOPROMPT);
			if (rc!=SQL_SUCCESS && rc!=SQL_SUCCESS_WITH_INFO) {
				auto [error, sqlstate] {get_error_msg(henv, hdbc, hstmt)};
				logger::log(LOGGER_SRC, "error", std::format("SQLDriverConnect failed: {}", error));
			} else {
				SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);	
				auto dbc = conn.get();
				if (dbc) {
					dbc->name = name;
					dbc->henv = henv;
					dbc->hdbc = hdbc;
					dbc->hstmt = hstmt;
					logger::log(LOGGER_SRC, "debug", std::format("connection to ODBC database established {:p} {}", static_cast<void*>(dbc), dbc->name));
				}
			}
		}
	
	};

	sql::recordset get_recordset(SQLHSTMT hstmt) 
	{
		sql::recordset rs;
		SQLSMALLINT numCols{0};
		SQLNumResultCols(hstmt, &numCols);
		
		auto _loop = [&numCols, &rs](auto& cols) {
			sql::record rec;
			rec.reserve( numCols );
			for ( auto& col: cols ) {
				if (col.dataSize > 0) {
					rec.try_emplace(col.colname, std::bit_cast<char*>(&col.data[0]));
				} else {
					rec.try_emplace(col.colname, "");
				}
			}
			rs.push_back(rec);			
		};
		
		if (numCols>0) {
			auto cols = bind_cols( hstmt, numCols );
			while ( SQLFetch( hstmt ) != SQL_NO_DATA ) {
				_loop(cols);
			}
		}
		return rs;
	}	

	//retrieve json response from resultset
	std::string read_json(SQLHSTMT hstmt) {
		std::string json;
		json.reserve(16383);
		int numRows{0};
		std::array<SQLCHAR, 8192> blob;
		SQLLEN bytes_read{0};
		while (SQLFetch(hstmt) != SQL_NO_DATA) {
			numRows++;
			SQLGetData(hstmt, 1, SQL_C_CHAR, blob.data(), blob.size(), &bytes_read);
			json.append(std::bit_cast<char*>(blob.data()));
		}
		if (!numRows)
			json.append("null");
		return json;
	}

	void get_json_array(SQLHSTMT hstmt, std::string &json) {
		json.append("[");
		SQLSMALLINT numCols{0};
		SQLNumResultCols( hstmt, &numCols );
		
		auto _loop = [&json](auto& cols) {
			for (auto& col: cols) {
				json.append("\"").append(col.colname).append("\":");
				if ( col.dataSize > 0 ) {
					if (col.dataType == SQL_TYPE_DATE || 
						col.dataType==SQL_TYPE_TIMESTAMP || 
						col.dataType==SQL_TYPE_TIME || 
						col.dataType==SQL_VARCHAR || 
						col.dataType==SQL_WVARCHAR || 
						col.dataType==SQL_CHAR) {
						json.append("\"").append(util::encode_json(std::bit_cast<char*>(&col.data[0]))).append("\"");
					} else {
						json.append( std::bit_cast<char*>(&col.data[0]) );
					}
				} else {
					json.append("\"\"");
				}
				json.append(",");
			}
		};
		
		if (numCols > 0) {
			auto cols = bind_cols( hstmt, numCols );
			while (SQLFetch(hstmt)!=SQL_NO_DATA) {
				json.append("{");
				_loop(cols);
				json.pop_back();
				json.append("},");
			}
			if (json.back() == ',')
				json.pop_back();
		}
	    json.append("]");
	}

	struct dbconns {
		constexpr static int MAX_CONNS {5};
		std::array<dbutil, MAX_CONNS> conns;
		int index {0};
		dbutil nulldb;

		constexpr auto get(std::string_view name, bool reset = false) noexcept
		{
			for (auto& db: conns) 
				if (db.name == name) {
					if (reset)
						db.reset_connection();
					return std::make_pair(true, &db);
				}
			return std::make_pair(false, &nulldb);
		}

		constexpr dbutil& add(std::string_view name, std::string_view connstr)
		{
			if (index == MAX_CONNS)
				throw sql::database_exception(std::format("dbconns::add() -> no more than {} database connections allowed: {}", MAX_CONNS, name));
			
			conns[index] = dbutil(name, connstr);
			++index;
			return conns[index - 1];
		}
	};


	constexpr dbutil& getdb(const std::string_view name, bool reset = false)
	{
	    thread_local dbconns dbc;
		
		if (auto [result, db]{dbc.get(name, reset)}; result) {
			return *db;
		} else {
			const std::string v {name};
			auto connstr {env::get_str(v)};
			return dbc.add(name, connstr);
		}
	}

	inline void retry(RETCODE rc, const std::string& dbname, const dbutil& db, int& retries, const std::string& sql)
	{
		auto [error_code, sqlstate, error_msg] {get_error_info(db.henv, db.hdbc, db.hstmt)};
		if (sqlstate == "HY000" || sqlstate == "01000" || sqlstate == "08S01" || rc == SQL_INVALID_HANDLE) {
			if (retries == max_retries) {
				throw sql::database_exception(std::format("retry() -> cannot connect to database:: {}", dbname));
			} else {
				retries++;
				getdb(dbname, true);
			}
		} else {
			throw sql::database_exception(std::format("db_exec() Error Code: {} SQLSTATE: {} {} -> sql: {}", error_code, sqlstate, error_msg, sql));
		}
	}	
	
	template<typename T, class FN>
	T db_exec(const std::string& dbname, const std::string& sql, FN func) 
	{
		std::string sqlcopy {sql};
		auto sqlcmd = (SQLCHAR*)sqlcopy.data();
		RETCODE rc {SQL_SUCCESS};
		int retries {0};

		while (true) {
			auto& db = getdb(dbname);
			rc = SQLExecDirect(db.hstmt, sqlcmd, SQL_NTS);
			if (rc != SQL_SUCCESS  && rc != SQL_NO_DATA)
				retry(rc, dbname, db, retries, sql);
			else 
				return func(db.hstmt);
		}
	}
	
}

namespace sql 
{
	bool has_rows(const std::string& dbname, const std::string& sql)
	{
		return db_exec<bool>(dbname, sql, [](SQLHSTMT hstmt) {
			recordset rs {get_recordset(hstmt)};
			SQLFreeStmt(hstmt, SQL_CLOSE);
			SQLFreeStmt(hstmt, SQL_UNBIND);
			return (!rs.empty());
		});
	}

	record get_record(const std::string& dbname, const std::string& sql)
	{
		return db_exec<record>(dbname, sql, [](SQLHSTMT hstmt) {
			record rec;
			recordset rs {get_recordset(hstmt)};
			SQLFreeStmt(hstmt, SQL_CLOSE);
			SQLFreeStmt(hstmt, SQL_UNBIND);
			if (!rs.empty())
				rec = rs[0];
			return rec;
		});
	}
	
	std::string get_json_response(const std::string& dbname, const std::string &sql)
	{
		return db_exec<std::string>(dbname, sql, [](SQLHSTMT hstmt) {
			std::string json {std::format(R"({{"status":"OK","data":{}}})", read_json(hstmt))};
			SQLFreeStmt(hstmt, SQL_CLOSE);
			SQLFreeStmt(hstmt, SQL_UNBIND);
			return json;
		});
	}
	
	std::string get_json_response_rs(const std::string& dbname, const std::string &sql, bool useDataPrefix, const std::string &prefixName)
	{
		return db_exec<std::string>(dbname, sql, [useDataPrefix, &prefixName](SQLHSTMT hstmt) {
			std::string json; 
			json.reserve(16383);
			if (useDataPrefix) {
				json.append( R"({"status":"OK",)" );
				json.append("\"");
				json.append(prefixName);
				json.append("\":");
			}
			get_json_array(hstmt, json);
			if (useDataPrefix)
				json.append("}");			
			SQLFreeStmt(hstmt, SQL_CLOSE);
			SQLFreeStmt(hstmt, SQL_UNBIND);
			return json;
		});
	}

	std::string get_json_response_rs(const std::string& dbname, const std::string &sql, const std::vector<std::string> &varNames, const std::string &prefixName) 
	{
		
		auto _loop = [&varNames](const SQLHSTMT& hstmt, std::string& json) {
			int rowsetCounter{0};
			do {
				json.append( "\"");
				json.append(varNames[rowsetCounter]);
				json.append("\":");
				get_json_array(hstmt, json);
				json.append(",");
				++rowsetCounter;
			} while (SQLMoreResults(hstmt) == SQL_SUCCESS);
		};
		
		return db_exec<std::string>(dbname, sql, [&_loop, &varNames, &prefixName](SQLHSTMT hstmt) {
			std::string json; 
			json.reserve(16383);
			json.append(R"({"status":"OK",)");
			json.append("\"");
			json.append(prefixName);
			json.append("\":{");
			_loop(hstmt, json);
			json.pop_back(); //remove last coma ","
			json.append("}}");
			SQLFreeStmt(hstmt, SQL_CLOSE);
			SQLFreeStmt(hstmt, SQL_UNBIND);
			return json;
		});
	}
	
	void exec_sql(const std::string& dbname, const std::string& sql)
	{
		return db_exec<void>(dbname, sql, [](SQLHSTMT hstmt) {
				SQLFreeStmt(hstmt, SQL_CLOSE);
		});
	}
	
	std::vector<recordset> get_rs(const std::string& dbname, const std::string &sql)
	{
		auto _loop = [](const SQLHSTMT& hstmt, std::vector<recordset>& vec) {
			do {
				vec.push_back(get_recordset(hstmt));
			} while (SQLMoreResults(hstmt) == SQL_SUCCESS);
		};
		
		return db_exec <std::vector<recordset>>(dbname, sql, [&_loop](SQLHSTMT hstmt) {
			std::vector<recordset> vec;
			_loop(hstmt, vec);
			SQLFreeStmt(hstmt, SQL_CLOSE);
			SQLFreeStmt(hstmt, SQL_UNBIND);
			return vec;
		});
	}
	
	std::string rs_to_json(const recordset& rs, const std::vector<std::string>& numeric_fields)
	{
		auto contains = [&numeric_fields](const auto& to_find) {
			return std::ranges::any_of(numeric_fields, [&to_find](const auto& v){return v == to_find;});
		};
		
		std::string json;
		json.reserve(4095);
		json.append("[");
		for (const auto& rec: rs) {
			json.append("{");
			for (const auto&[key, value]: rec) {
				if (contains(key))
					json.append(std::format(R"("{}":{},)", key, !value.empty() ? value : "null"));
				else
					json.append(std::format(R"("{}":"{}",)", key, util::encode_json(value)));
			}
			json.pop_back();
			json.append("},");
		}
		json.pop_back();
		json.append("]");
		return json;
	}
}


