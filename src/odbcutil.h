#ifndef ODBCUTIL_H_
#define ODBCUTIL_H_

#include <sql.h>
#include <sqlext.h>
#include <format>
#include "logger.h"

struct dbconn {
	SQLHENV henv{SQL_NULL_HENV};
	SQLHDBC hdbc{SQL_NULL_HDBC};
	SQLHSTMT hstmt{SQL_NULL_HSTMT};
	std::string name{"N/A"};
	~dbconn();
};

#endif
