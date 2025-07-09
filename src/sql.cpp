#include "sql.h"

namespace 
{
	//using namespace sql::detail;
	
	constexpr int max_retries {10};

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

	inline void retry(RETCODE rc, const std::string& dbname, const sql::detail::dbutil& db, int& retries, const std::string& sql)
	{
		auto [error_code, sqlstate, error_msg] {sql::detail::get_error_info(db.henv, db.hdbc, db.hstmt)};
		if (sqlstate == "HY000" || sqlstate == "01000" || sqlstate == "08S01" || rc == SQL_INVALID_HANDLE) {
			if (retries == max_retries) {
				throw sql::database_exception(std::format("retry() -> cannot connect to database:: {}", dbname));
			} else {
				retries++;
				sql::detail::getdb(dbname, true);
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
			auto& db = sql::detail::getdb(dbname);
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


