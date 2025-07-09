#include "odbcutil.h"
#include <utility> // Required for std::move

// Move constructor implementation.
// "Steals" the handles from the 'other' object and leaves it in a valid, empty state.
dbconn::dbconn(dbconn&& other) noexcept
    // Member-wise move
    : henv(other.henv),
      hdbc(other.hdbc),
      hstmt(other.hstmt),
      name(std::move(other.name)) {

    // Nullify the handles in the source object so its destructor doesn't
    // free the resources that have been moved.
    other.henv = SQL_NULL_HENV;
    other.hdbc = SQL_NULL_HDBC;
    other.hstmt = SQL_NULL_HSTMT;

    logger::log("odbcutil", "debug", std::format("ODBC connection {:p} move-constructed from {:p}", static_cast<void*>(this), static_cast<void*>(&other)));
}

// Move assignment operator implementation.
dbconn& dbconn::operator=(dbconn&& other) noexcept {
    // Prevent self-assignment
    if (this != &other) {
        // Release any resources this object currently owns.
        if (henv != SQL_NULL_HENV) {
            SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
            SQLDisconnect(hdbc);
            SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
            SQLFreeHandle(SQL_HANDLE_ENV, henv);
        }

        // Steal the resources from the 'other' object.
        henv = other.henv;
        hdbc = other.hdbc;
        hstmt = other.hstmt;
        name = std::move(other.name);

        // Leave the 'other' object in a valid, empty state.
        other.henv = SQL_NULL_HENV;
        other.hdbc = SQL_NULL_HDBC;
        other.hstmt = SQL_NULL_HSTMT;

        logger::log("odbcutil", "debug", std::format("ODBC connection {:p} move-assigned from {:p}", static_cast<void*>(this), static_cast<void*>(&other)));
    }
    return *this;
}

// Destructor implementation.
// It will now only free handles if they are not null.
dbconn::~dbconn() {
    if (henv != SQL_NULL_HENV) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        SQLDisconnect(hdbc);
        SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
        SQLFreeHandle(SQL_HANDLE_ENV, henv);
    }
}
