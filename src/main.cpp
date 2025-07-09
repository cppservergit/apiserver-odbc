#include "server.h"

int main()
{
	server s;
	
	s.register_webapi
	(
		webapi_path("/api/shippers/view"), 
		"List of shipping companies",
		http::verb::GET, 
		[](http::request& req) 
		{
			auto json {sql::get_json_response("DB1", "sp_shippers_view")};
			req.response.set_body( json );
		}
	);

	s.register_webapi
	(
		webapi_path("/api/products/view"), 
		"List of products",
		http::verb::GET, 
		rules {},
		roles {} ,
		[](http::request& req) 
		{
			req.response.set_body( sql::get_json_response("DB1", "sp_products_view") );
		}
	);


	s.register_webapi
	(
		webapi_path("/api/customer/search"), 
		"Find customers by company's name",
		http::verb::POST,             
		rules { 
			{"filter", http::field_type::STRING, true} 
		},
		roles {
			"sysadmin", 
			"customer_info"
		},
		[](http::request& req) 
		{
			auto sql {req.get_sql("sp_customers_like $filter")};
			req.response.set_body(sql::get_json_response("DB1", sql));
		}
	);

	s.register_webapi
	(
		webapi_path("/api/customer/info"), 
		"Retrieve customer record and the list of his purchase orders",
		http::verb::GET, 
		rules {
			{"customerid", http::field_type::STRING, true}
		}, 	
		roles {"customer_access", "sysadmin"},
		[](http::request& req)
		{
			auto sql {req.get_sql("sp_customer_get $customerid")};
			req.response.set_body(sql::get_json_response("DB1", sql));
		}
	);

	s.register_webapi
	(
		webapi_path("/api/sales/query"), 
		"Sales report by category for a period",
		http::verb::POST, 
		rules {
			{"date1", http::field_type::DATE, true},
			{"date2", http::field_type::DATE, true}
		}, 	
		roles {"customer_access", "sysadmin"},
		[](http::request& req)
		{
			auto sql {req.get_sql("sp_sales_by_category $date1, $date2")};
			req.response.set_body(sql::get_json_response("DB1", sql));
		}
	);

	s.register_webapi
	(
		webapi_path("/api/blob/add"), 
		"Upload document to filesystem and register it in database",
		http::verb::POST, 
		rules { 
			{"title", http::field_type::STRING, true},
			{"document.document", http::field_type::STRING, true},
			{"document.filename", http::field_type::STRING, true},
			{"document.content_type", http::field_type::STRING, true},
			{"document.content_len", http::field_type::INTEGER, true}
		}, 	
		roles {"general", "sysadmin"},
		[](http::request& req) 
		{
			auto sql {req.get_sql("sp_blob_add $title, $document.document, $document.filename, $document.content_type, $document.content_len")};
			sql::exec_sql("DB1", sql);
			req.response.set_body(R"({"status":"OK"})");
		}
	);

	s.register_webapi
	(
		webapi_path("/api/blob/update"), 
		"Upload document to filesystem and register it in database",
		http::verb::POST, 
		rules { 
			{"blob_id", http::field_type::INTEGER, true},
			{"title", http::field_type::STRING, true},
			{"document.document", http::field_type::STRING, true},
			{"document.filename", http::field_type::STRING, true},
			{"document.content_type", http::field_type::STRING, true},
			{"document.content_len", http::field_type::INTEGER, true}
		}, 	
		roles {"general", "sysadmin"} ,
		[](http::request& req) 
		{
			if (auto rec {sql::get_record("DB1", req.get_sql("sp_blob_get_uuid $blob_id"))}; !rec.empty()) {
				std::string path{http::blob_path + rec["document"]};
				std::remove(path.c_str());
			}
			auto sql {req.get_sql("sp_blob_update $blob_id, $title, $document.document, $document.filename, $document.content_type, $document.content_len")};
			sql::exec_sql("DB1", sql);
			req.response.set_body(R"({"status":"OK"})");
		}
	);

	s.register_webapi
	(
		webapi_path("/api/blob/get"), 
		"Retrieve blob record",
		http::verb::GET, 
		rules {
			{"id", http::field_type::INTEGER, true}
		},
		roles {} ,
		[](http::request& req) 
		{
			req.response.set_body(sql::get_json_response("DB1", req.get_sql("sp_blob_getrow $id")));
		}
	);

	s.register_webapi
	(
		webapi_path("/api/blob/view"), 
		"List of uploaded documents",
		http::verb::GET, 
		[](http::request& req) 
		{
			req.response.set_body(sql::get_json_response("DB1", "sp_blob_view"));
		}
	);

	s.register_webapi
	(
		webapi_path("/api/blob/delete"), 
		"Delete document record and its associated file",
		http::verb::GET, 
		rules {
			{"id", http::field_type::INTEGER, true}
		},
		roles {"can_delete", "sysadmin"},
		[](http::request& req) 
		{
			auto rec {sql::get_record("DB1", req.get_sql("sp_blob_get_uuid $id"))};
			if (!rec.empty()) {
				std::string path{http::blob_path + rec["document"]};
				std::remove(path.c_str());
				sql::exec_sql("DB1", req.get_sql("sp_blob_delete $id"));
			}
			req.response.set_body(R"({"status":"OK"})");
		}
	);

	s.register_webapi
	(
		webapi_path("/api/blob/download"), 
		"Download file associated with a document",
		http::verb::GET, 
		rules {
			{"id", http::field_type::INTEGER, true}
		},
		roles {} ,
		[](http::request& req) 
		{
			auto rec {sql::get_record("DB1", req.get_sql("sp_blob_get $id"))};
			if (!rec.empty()) {
				std::string path{http::blob_path + rec["document"]};
				std::string filename {rec["filename"]};
				std::string content_type {rec["content_type"]};
				std::ostringstream buffer;
				{
					std::ifstream file{path};
					if (file.is_open()) {
						buffer << file.rdbuf();
						req.response.set_content_disposition(std::format(R"(attachment; filename="{}";)", filename));
						req.response.set_body_blob(buffer.view(), content_type);
					} else {
						req.log("service", "error", "/api/blob/download -> cannot open file - user: $userlogin blob id: $id");
						const std::string error{std::format("Error downloading file: {} with ID: {}", filename, rec["document"])};
						req.response.set_content_disposition(R"(attachment; filename="error.txt";)");
						req.response.set_body_blob(error, "text/plain");
					}
				}
			} else {
				throw http::resource_not_found_exception("blob ID " + req.get_param("id"));
			}
		}
	);

	s.register_webapi
	(
		webapi_path("/api/categ/view"), 
		"List of expense categories",
		http::verb::GET, 
		rules {},
		roles {} ,
		[](http::request& req) 
		{
			req.response.set_body(sql::get_json_response("DB1", "sp_categ_view"));
		}
	);

	s.register_webapi
	(
		webapi_path("/api/categ/get"), 
		"Retrieve category record",
		http::verb::GET, 
		rules {
			{"id", http::field_type::INTEGER, true}
		},
		roles {} ,
		[](http::request& req) 
		{
			req.response.set_body(sql::get_json_response("DB1", req.get_sql("sp_categ_get $id")));
		}
	);

	s.register_webapi
	(
		webapi_path("/api/categ/delete"), 
		"Delete category record",
		http::verb::GET, 
		rules {
			{"id", http::field_type::INTEGER, true}
		},
		roles {"can_delete"},
		[](http::request& req) 
		{
			//validator for referential integrity
			req.enforce("validator_ref_integrity", "err.delete", [&req]()-> bool { 
				return !sql::has_rows("DB1", req.get_sql("sp_categ_in_use $id"));
			});
			sql::exec_sql("DB1", req.get_sql("sp_categ_delete $id"));
			req.response.set_body(R"({"status":"OK"})");
		}
	);

	s.register_webapi
	(
		webapi_path("/api/categ/add"), 
		"Add category record",
		http::verb::POST, 
		rules {
			{"descrip", http::field_type::STRING, true}
		},
		roles {},
		[](http::request& req) 
		{
			sql::exec_sql("DB1", req.get_sql("sp_categ_add $descrip"));
			req.response.set_body(R"({"status":"OK"})");
			req.send_mail(req.user_info.mail, "Category created via API-Server++", "test.html");
		}
	);

	s.register_webapi
	(
		webapi_path("/api/categ/update"), 
		"Update category record",
		http::verb::POST, 
		rules {
			{"categ_id", http::field_type::INTEGER, true},
			{"descrip", http::field_type::STRING, true}
		},
		roles {"can_update"},
		[](http::request& req) 
		{
			sql::exec_sql("DB1", req.get_sql("sp_categ_update $categ_id, $descrip"));
			req.response.set_body(R"({"status":"OK"})");
		}
	);
	
	s.register_webapi
	(
		webapi_path("/api/gasto/view"), 
		"List expenses",
		http::verb::GET, 
		rules {},
		roles {} ,
		[](http::request& req) 
		{
			req.response.set_body(sql::get_json_response("DB1", "sp_gasto_view"));
		}
	);

	s.register_webapi
	(
		webapi_path("/api/gasto/get"), 
		"Retrieve expense record",
		http::verb::GET, 
		rules {
			{"id", http::field_type::INTEGER, true}
		},
		roles {} ,
		[](http::request& req) 
		{
			req.response.set_body(sql::get_json_response("DB1", req.get_sql("sp_gasto_get $id")));
		}
	);

	s.register_webapi
	(
		webapi_path("/api/gasto/delete"), 
		"Delete expense record",
		http::verb::GET, 
		rules {
			{"id", http::field_type::INTEGER, true}
		},
		roles {"can_delete"},
		[](http::request& req) 
		{
			sql::exec_sql("DB1", req.get_sql("sp_gasto_delete $id"));
			req.response.set_body(R"({"status":"OK"})");
		}
	);

	s.register_webapi
	(
		webapi_path("/api/gasto/add"), 
		"Add expense record",
		http::verb::POST, 
		rules {
			{"fecha", http::field_type::DATE, true},
			{"categ_id", http::field_type::INTEGER, true},
			{"monto", http::field_type::DOUBLE, true},
			{"motivo", http::field_type::STRING, true}			
		},
		roles {} ,
		[](http::request& req) 
		{
			req.enforce("validator_today", "err.invaliddate", [&req]()-> bool { 
				return req.get_param("fecha") <= util::today();
			});			
			sql::exec_sql("DB1", req.get_sql("sp_gasto_insert $fecha, $categ_id, $monto, $motivo"));
			req.response.set_body(R"({"status":"OK"})");
		}
	);

	s.register_webapi
	(
		webapi_path("/api/gasto/update"), 
		"Update expense record",
		http::verb::POST, 
		rules {
			{"gasto_id", http::field_type::INTEGER, true},
			{"fecha", http::field_type::DATE, true},
			{"categ_id", http::field_type::INTEGER, true},
			{"monto", http::field_type::DOUBLE, true},
			{"motivo", http::field_type::STRING, true}			
		},
		roles {"can_update"},
		[](http::request& req) 
		{
			req.enforce("validator_today", "err.invaliddate", [&req]()-> bool { 
				return req.get_param("fecha").compare(util::today()) <= 0;
			});				
			auto sql {req.get_sql("sp_gasto_update $gasto_id, $fecha, $categ_id, $monto, $motivo")};
			sql::exec_sql("DB1", sql);
			req.response.set_body(R"({"status":"OK"})");
		}
	);	
	
	s.start();
    return EXIT_SUCCESS;
}
