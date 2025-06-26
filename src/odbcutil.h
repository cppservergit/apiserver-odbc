#ifndef ODBCUTIL_H_
#define ODBCUTIL_H_

#include <sql.h>
#include <sqlext.h>
#include <string>
#include <format>
#include "logger.h"

// This struct now follows the Rule of Five.
// It manages a unique ODBC connection resource, so it is movable but not copyable.
struct dbconn {
	SQLHENV henv{SQL_NULL_HENV};
	SQLHDBC hdbc{SQL_NULL_HDBC};
	SQLHSTMT hstmt{SQL_NULL_HSTMT};
	std::string name{"N/A"};

	// Default constructor
	dbconn() = default;

	// 1. Destructor (your original implementation)
	~dbconn();

	// 2. Copy Constructor (deleted)
	// We delete the copy constructor because copying a database connection
	// is not a well-defined operation. This prevents accidental copies.
	dbconn(const dbconn&) = delete;

	// 3. Copy Assignment Operator (deleted)
	// Deleted for the same reason as the copy constructor.
	dbconn& operator=(const dbconn&) = delete;

	// 4. Move Constructor
	// Allows efficient transfer of ownership of the connection from a temporary
	// object to a new one.
	dbconn(dbconn&& other) noexcept;

	// 5. Move Assignment Operator
	// Allows efficient transfer of ownership from one object to another existing one.
	dbconn& operator=(dbconn&& other) noexcept;
};

#endif // ODBCUTIL_H_
