# API-Server++ for ODBC

Easy to build Web APIs with Modern C++ and a minimal code framework using imperative/functional programming style. Fast, secure, and well-documented.

```
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
			req.response.set_body(sql::get_json_response("DB1", "sp_shipper_view"));
		}
	);
	
	s.start();
}
```

This is the declaration of the utility function used to register an API with all its features (a simplified version was used above):
```
	void register_webapi(
		const webapi_path& _path, 
		const std::string& _description, 
		http::verb _verb, 
		const std::vector<http::input_rule>& _rules, 
		const std::vector<std::string>& _roles, 
		std::function<void(http::request&)> _fn,
		bool _is_secure = true
	);
```
You can specify input rules (input parameters, optional), authorized roles (optional), and your lambda function, which most of the time will be very simple, but it can also incorporate additional validations. All this metadata will be used to auto-generate API documentation.

API-Server++ is a compact single-threaded EPOLL HTTP 1.1 microserver for Linux, for serving API requests only (GET/POST/OPTIONS), when a request arrives, the corresponding lambda will be dispatched for execution to a background thread, using the one-producer/many-consumers model. This way API-Server++ can multiplex thousands of concurrent connections with a single thread dispatching all the network-related tasks. API-Server++ is an async, non-blocking, event-oriented server, async because of the way the tasks are dispatched, it returns immediately to keep processing network events, while a background thread picks the task and executes it. The kernel will notify the program when there are events to process, in which case, non-blocking operations will be used on the sockets, and the program won't consume CPU while waiting for events, this way a single-threaded server can serve thousands of concurrent clients if the I/O tasks are fast. The size of the workers' thread pool can be configured via environment variable, the default is 4, which has proved to be good enough for high loads on VMs with 4-6 virtual cores.

API-Server++ was designed to be run as a container on Kubernetes, with a stateless security/session model based on JSON web token (good for scalability), and built-in observability features for Grafana stack, but it can be run as a regular program on a terminal for development or as a SystemD Linux service for production, tightly integrated with native Linux log facilities, on production it will run behind an Ingress or Load Balancer providing TLS and Layer-7 protection.

It uses direct calls to the ODBC C API for maximum speed, as well as `libcurl` for secure email and `openssl v3` for JWT signatures. It expects a JSON response from queries returning data, which is very easy to do with stored procedures in most modern databases.

![image](https://github.com/cppservergit/apiserver-odbc/assets/126841556/ab9c74b9-097f-4899-a564-b46d3cc931c1)


## Requirements

The test environment is Ubuntu 23.10 with GCC 13.2, We used Canonical's Multipass VMs on Windows 10 Pro, it's a very agile tool for managing lightweight VMs on Windows, you can create an Ubuntu 23.10 VM using a command like this, with very few resources:
```
multipass launch -n testvm -c 4 -m 2g -d 6g mantic
```
If you are not going to update the whole operating system then you can use `-d 4g` for 4GB of disk space.

Update Ubuntu package list:
```
sudo apt update
```

Install required packages:
```
sudo apt install g++-13 libssl-dev libcurl4-openssl-dev uuid-dev libjson-c-dev libldap-dev unixodbc-dev tdsodbc make -y --no-install-recommends
```

Optionally, if your VM has enough disk space (10GB) you can upgrade the rest of the operating system, it may take some minutes and require a restart of the VM:
```
sudo apt upgrade -y
```

__Notes__: 

* You can run API-Server++ on Ubuntu 22.04 if you create a native Linux LXD container with Ubuntu 23.10 to run the API-Server++ binary and use HAProxy as the HTTPS front on Ubuntu 22.04 (the server host OS), this way you can run on a reliable LTS Ubuntu server, and also protect network access to API-Server++, which is only visible from the host via HAProxy.

![image](https://github.com/cppservergit/apiserver-odbc/assets/126841556/b168d1f7-28b3-40f2-8bfb-78eb68045916)

* Starting on April 2024 Ubuntu 24.04 LTS can be used for production on the host and the container.
* At present the Microsoft ODBC Driver can be used on Ubuntu 23.04, this would be a constraint for the LXD container if you want to use this driver, otherwise you can use FreeTDS ODBC driver (for Sybase too) with more recent versions of Ubuntu Server.

API-Server++ requires GCC 13.1 or newer because it does take advantage of the latest C++ 20/23 standard features that are only supported by GCC 13.1 onwards, like `<format>`, `constexpr` strings and functions, ranges, and more. This way API-Server++ achieves an "A" qualification with SonarQube static analyzer

### Demo database setup setup

Download backups for SQLServer:
```
curl https://cppserver.com/files/apiserver/testdb.bak -O
curl https://cppserver.com/files/apiserver/demodb.bak -O

```

Please restore these backups in your SQLServer development instance, DemoDB contains several tables to exercise different kinds of APIs, while TestDB contains the minimal security tables and the stored procedure `cpp_dblogin` to support an SQL-based login mechanism, so you can test API-Server++ JWT (JSON web token) implementation. You can use your database for API testing, but for security purposes, TestDB is required, although it can be replaced later by your own security DB, as long as you provide a compatible `cpp_dblogin` stored procedure, more details will be provided in a forward section of this README.

## Build

Retrieve the latest version of API-Server++ ODBC
```
git clone https://github.com/cppservergit/apiserver-odbc
```

Navigate into the API-Server++ directory
```
cd apiserver-odbc
```

Compile and build executable
```
make
```

Expected output:
```
g++-13 -Wall -Wextra -O3 -std=c++23 -pthread -flto=4 -fno-extern-tls-init -march=x86-64 -mtune=intel -c src/env.cpp
g++-13 -Wall -Wextra -O3 -std=c++23 -pthread -flto=4 -fno-extern-tls-init -march=x86-64 -mtune=intel -c src/logger.cpp
g++-13 -Wall -Wextra -O3 -std=c++23 -pthread -flto=4 -fno-extern-tls-init -march=x86-64 -mtune=intel -c src/jwt.cpp
g++-13 -Wall -Wextra -O3 -std=c++23 -pthread -flto=4 -fno-extern-tls-init -march=x86-64 -mtune=intel -c src/httputils.cpp
g++-13 -Wall -Wextra -O3 -std=c++23 -pthread -flto=4 -fno-extern-tls-init -march=x86-64 -mtune=intel -c src/sql.cpp
g++-13 -Wall -Wextra -O3 -std=c++23 -pthread -flto=4 -fno-extern-tls-init -march=x86-64 -mtune=intel -c src/login.cpp
g++-13 -Wall -Wextra -O3 -std=c++23 -pthread -flto=4 -fno-extern-tls-init -march=x86-64 -mtune=intel -DCPP_BUILD_DATE=20230807 -c src/server.cpp
g++-13 -Wall -Wextra -O3 -std=c++23 -pthread -flto=4 -fno-extern-tls-init -march=x86-64 -mtune=intel -c src/main.cpp
g++-13 -Wall -Wextra -O3 -std=c++23 -pthread -flto=4 -fno-extern-tls-init -march=x86-64 -mtune=intel env.o logger.o jwt.o httputils.o sql.o login.o server.o main.o -lodbc -lcurl -lcrypto -luuid -ljson-c -o "apiserver"
```

## Run API-Server++

Please edit run script and fix the ODBC connection strings to meet your environment:
```
nano run
```

Search for these entries and change the SERVER, DATABASE, and PWD properties according to your environment:
```
# ODBC SQL authenticator config
export CPP_LOGINDB="Driver=FreeTDS;SERVER=demodb.mshome.net;PORT=1433;DATABASE=testdb;UID=sa;PWD=basica;APP=CPPServer;Encryption=off;ClientCharset=UTF-8"
# ODBC SQL data sources
export DB1="Driver=FreeTDS;SERVER=demodb.mshome.net;PORT=1433;DATABASE=demodb;UID=sa;PWD=basica;APP=CPPServer;Encryption=off;ClientCharset=UTF-8"
```

Default script:
```
#!/bin/bash
export CPP_LOGIN_LOG=1
export CPP_HTTP_LOG=1
export CPP_PORT=8080
export CPP_POOL_SIZE=4
# JWT config - NOTE: it is vital to use a hard-to-guess secret
export CPP_JWT_SECRET="Basica123*"
export CPP_JWT_EXP=600
# ODBC SQL authenticator config
export CPP_LOGINDB="Driver=FreeTDS;SERVER=demodb.mshome.net;PORT=1433;DATABASE=testdb;UID=sa;PWD=basica;APP=CPPServer;Encryption=off;ClientCharset=UTF-8"
# ODBC SQL data sources
export DB1="Driver=FreeTDS;SERVER=demodb.mshome.net;PORT=1433;DATABASE=demodb;UID=sa;PWD=basica;APP=CPPServer;Encryption=off;ClientCharset=UTF-8"
# secure mail config
export CPP_MAIL_SERVER="smtp://smtp.gmail.com:587"
export CPP_MAIL_USER="admin@martincordova.com"
export CPP_MAIL_PWD="your-smtp-password"
# LDAP authenticator config
export CPP_LDAP_URL="ldap://demodb.mshome.net:1389/"
export CPP_LDAP_ADMIN_USER_DN="cn=admin,dc=example,dc=org"
export CPP_LDAP_ADMIN_PWD="basica"
export CPP_LDAP_USER_DN="cn={userid},ou=users,dc=example,dc=org"
export CPP_LDAP_USER_BASE="ou=users,dc=example,dc=org"
export CPP_LDAP_USERGROUPS_BASE="ou=users,dc=example,dc=org"
export CPP_LDAP_USER_FILTER="(userid={userid})"
export CPP_LDAP_USERGROUPS_FILTER="(member={dn})"
./apiserver
```
CRTL-x to save your changes.

Make it executable:
```
chmod +x run
```

Run API-Server++
```
./run
```

Expected output:
```
{"source":"signal","level":"info","msg":"signal interceptor registered","thread":"139909296822784","x-request-id":""}
{"source":"server","level":"info","msg":"registering built-in diagnostic and security services...","thread":"139909296822784","x-request-id":""}
{"source":"server","level":"info","msg":"registered (insecure) WebAPI for path: /api/ping","thread":"139909296822784","x-request-id":""}
{"source":"server","level":"info","msg":"registered (insecure) WebAPI for path: /api/version","thread":"139909296822784","x-request-id":""}
{"source":"server","level":"info","msg":"registered (insecure) WebAPI for path: /api/sysinfo","thread":"139909296822784","x-request-id":""}
{"source":"server","level":"info","msg":"registered (insecure) WebAPI for path: /api/metrics","thread":"139909296822784","x-request-id":""}
{"source":"server","level":"info","msg":"registered (insecure) WebAPI for path: /api/login","thread":"139909296822784","x-request-id":""}
{"source":"server","level":"info","msg":"registered (insecure) WebAPI for path: /api/docs","thread":"139909296822784","x-request-id":""}
{"source":"env","level":"info","msg":"port: 8080","thread":"139909296822784","x-request-id":""}
{"source":"env","level":"info","msg":"pool size: 4","thread":"139909296822784","x-request-id":""}
{"source":"env","level":"info","msg":"login log: 1","thread":"139909296822784","x-request-id":""}
{"source":"env","level":"info","msg":"http log: 1","thread":"139909296822784","x-request-id":""}
{"source":"env","level":"info","msg":"jwt exp: 600","thread":"139909296822784","x-request-id":""}
{"source":"server","level":"info","msg":"Pod: testvm PID: 2958 starting API-Server++ v1.0.7-20240306","thread":"139909296822784","x-request-id":""}
{"source":"server","level":"info","msg":"hardware threads: 4 GCC: 13.2.0","thread":"139909296822784","x-request-id":""}
{"source":"pool","level":"info","msg":"starting worker thread","thread":"139909283182272","x-request-id":""}
{"source":"pool","level":"info","msg":"starting worker thread","thread":"139909274789568","x-request-id":""}
{"source":"pool","level":"info","msg":"starting worker thread","thread":"139909266396864","x-request-id":""}
{"source":"pool","level":"info","msg":"starting worker thread","thread":"139909258004160","x-request-id":""}
{"source":"server","level":"info","msg":"server started in 0.000355605s","thread":"139909296822784","x-request-id":""}
{"source":"epoll","level":"info","msg":"starting epoll FD: 4","thread":"139909296822784","x-request-id":""}
{"source":"epoll","level":"info","msg":"listen socket FD: 5 port: 8080","thread":"139909296822784","x-request-id":""}
```

## Test connection to API-Server++

Open another terminal on your VM and execute:
```
curl localhost:8080/api/version
```

Expected output:
```
{"status": "OK", "data":[{"pod": "test", "server": "API-Server++ v1.0.7-20240306"}]}
```

## Test login API and JWT

Test login API (tables s_user, s_role and s_user_role store the security configuration in the public schema of testdb):
```
curl localhost:8080/api/login -F "username=mcordova" -F "password=basica"
```

Expected output (token will vary):
```
{"status":"OK","data":[{"displayname":"Martín Córdova","token_type":"bearer","id_token":"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJsb2dpbiI6Im1jb3Jkb3ZhIiwibWFpbCI6ImNwcHNlcnZlckBtYXJ0aW5jb3Jkb3ZhLmNvbSIsInJvbGVzIjoiY2FuX2RlbGV0ZSwgY2FuX3VwZGF0ZSwgc3lzYWRtaW4iLCJleHAiOjE2OTE0NTQ1OTl9.M2i47hipMt9CxlPPA1zNeIpVIJiPfsMSiVJe0G7ZXHE"}]}
```

API-Server++ accepts multipart-form as input, as well as JSON, the same request can be sent as:
```
curl --json '{"username":"mcordova", "password":"basica"}' localhost:8080/api/login
```

## Stop the server

On the same terminal where API-Server++ is running press CTRL-C to stop the server, you should see some messages like these on the console:
```
{"source":"signal","level":"info","msg":"stop signal received via epoll","thread":"140693621230080","x-request-id":""}
{"source":"epoll","level":"info","msg":"closing listen socket FD: 5","thread":"140693621230080","x-request-id":""}
{"source":"epoll","level":"info","msg":"closing epoll FD: 4","thread":"140693621230080","x-request-id":""}
{"source":"server","level":"info","msg":"testvm shutting down...","thread":"140693621230080","x-request-id":""}
{"source":"pool","level":"info","msg":"stopping worker thread","thread":"140693607544512","x-request-id":""}
{"source":"pool","level":"info","msg":"stopping worker thread","thread":"140693456541376","x-request-id":""}
{"source":"pool","level":"info","msg":"stopping worker thread","thread":"140693599151808","x-request-id":""}
{"source":"pool","level":"info","msg":"stopping worker thread","thread":"140693590759104","x-request-id":""}
{"source":"odbcutil","level":"debug","msg":"closing ODBC connection 0x7ff5b4000de0 CPP_LOGINDB","thread":"140693590759104","x-request-id":""}
{"source":"odbcutil","level":"debug","msg":"closing ODBC connection 0x7ff5c0000e40 CPP_LOGINDB","thread":"140693607544512","x-request-id":""}
```
All resources were released, including ODBC resources. API-Server++ intercepts Linux signals, it can be stopped via CTRL-C or SystemD-triggered signals, container orchestrators like Kubernetes also use these signals to kill Pods (a running container).

## Hello World - your first API

Edit main.cpp to add your first API definition:
```
nano src/main.cpp
```

Add this code below `server s;` and right above `s.start();`:
```
	s.register_webapi
	(
		webapi_path("/api/shippers/view"), 
		"List of shipping companies",
		http::verb::GET, 
		{} /* inputs */, 	
		{} /* roles */,
		[](http::request& req) 
		{
			req.response.set_body(sql::get_json_response("DB1", "sp_shipper_view"));
		}
	);
```
CTRL-x to exit and save.
With one line of code, we define a new API, with some metadata including a description, the HTTP method supported, input rules validation if any, authorized roles if any, and most importantly, a lambda function with the code implementing the API, a one-liner in this case, thanks to the high-level abstractions of API-Server++.

The function `sql::get_json_response()` executes a query that MUST return JSON straight from the database, in the specific case of the HelloWorld example the invoked SQL object looks like this:

```
CREATE procedure [dbo].[sp_shippers_view] as
begin
	set nocount on
	SELECT 
		shipperid,
		companyname,
		phone
	FROM 
		shippers WITH (NOLOCK)
	FOR JSON AUTO 
end
```
The DemoDB database contains several examples of stored procedures that return JSON, including this one, your SPs should follow this pattern because API-Server++ `sql::get_json_response()` function relies on the Database to generate the JSON output from queries returning resultsets. In some cases, an SP does not return anything because it will only affect data without returning a resultset, in other cases if your database is not capable of returning JSON from resultsets, API-Server++ takes charge of generating the JSON from an ODBC resultset returned by the SP.

The whole program should look like this:
```
#include "server.h"

int main()
{
	server s;

	s.register_webapi
	(
		webapi_path("/api/shippers/view"), 
		"List of shipping companies",
		http::verb::GET, 
		{} /* inputs */, 	
		{} /* roles */,
		[](http::request& req) 
		{
			req.response.set_body(sql::get_json_response("DB1", "sp_shippers_view"));
		}
	);

        s.start();
}
```

Now recompile, only the main.cpp module will be recompiled and the program relinked with the object files, it's a quick operation:
```
make
```

Expected output:
```
g++-13 -Wall -Wextra -O3 -std=c++23 -pthread -flto=4 -fno-extern-tls-init -march=native -mtune=intel -DCPP_BUILD_DATE=20240306 -c src/main.cpp
g++-13 -Wall -Wextra -O3 -std=c++23 -pthread -flto=4 -fno-extern-tls-init -march=native -mtune=intel env.o logger.o jwt.o httputils.o email.o odbcutil.o sql.o login.o util.o main.o -lodbc -lcurl -lcrypto -luuid -ljson-c -o "apiserver"
```

Now run the server again:
```
./run
```

Now starting the log output (2nd line) you should see this line:
```
{"source":"server","level":"info","msg":"registered WebAPI for path: /api/shippers/view"}
```

Now that the API-Server++ is running again and your API has been published, let's test it with CURL in the 2nd terminal we used before, first, we need to login to obtain a [JWT token](https://jwt.io/introduction), otherwise, any attempt to invoke your API will be rejected with HTTP status code 401 (login required error).

```
curl localhost:8080/api/login -F "username=mcordova" -F "password=basica"
```

Expected output:
```
{"status":"OK","data":[{"displayname":"Martín Córdova","token_type":"bearer","id_token":"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJsb2dpbiI6Im1jb3Jkb3ZhIiwibWFpbCI6ImNwcHNlcnZlckBtYXJ0aW5jb3Jkb3ZhLmNvbSIsInJvbGVzIjoiY2FuX2RlbGV0ZSwgY2FuX3VwZGF0ZSwgc3lzYWRtaW4iLCJleHAiOjE2OTE0Njc3MTR9.18g9mAXNkbXAxxP1i6rGKR1IKWAIuLpFAAkwaN8Jmjc"}]}
```

Mark and copy the token value only, without the quotes, in this example, it would be:
```
eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJsb2dpbiI6Im1jb3Jkb3ZhIiwibWFpbCI6ImNwcHNlcnZlckBtYXJ0aW5jb3Jkb3ZhLmNvbSIsInJvbGVzIjoiY2FuX2RlbGV0ZSwgY2FuX3VwZGF0ZSwgc3lzYWRtaW4iLCJleHAiOjE2OTE0Njc3MTR9.18g9mAXNkbXAxxP1i6rGKR1IKWAIuLpFAAkwaN8Jmjc
```

Now invoke your HelloWorld API with curl, passing the proper header with the token, something like `-H "Authorization: Bearer xyz123..."`
```
curl localhost:8080/api/shippers/view -H "Authorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJsb2dpbiI6Im1jb3Jkb3ZhIiwibWFpbCI6ImNwcHNlcnZlckBtYXJ0aW5jb3Jkb3ZhLmNvbSIsInJvbGVzIjoiY2FuX2RlbGV0ZSwgY2FuX3VwZGF0ZSwgc3lzYWRtaW4iLCJleHAiOjE2OTE0Njc3MTR9.18g9mAXNkbXAxxP1i6rGKR1IKWAIuLpFAAkwaN8Jmjc"
```

Expected output:
```
{
  "status": "OK",
  "data": [
    {
      "shipperid": 1,
      "companyname": "Speedy Express",
      "phone": "(503) 555-9831"
    },
    {
      "shipperid": 2,
      "companyname": "United Package",
      "phone": "(505) 555-3199"
    },
    {
      "shipperid": 3,
      "companyname": "Federal Shipping",
      "phone": "(503) 555-9931"
    },
    {
      "shipperid": 13,
      "companyname": "Federal Courier Venezuela",
      "phone": "555-6728"
    },
    {
      "shipperid": 501,
      "companyname": "UPS",
      "phone": "500-CALLME"
    },
    {
      "shipperid": 503,
      "companyname": "Century 22 Courier",
      "phone": "800-WE-CHARGE"
    }
  ]
}
```

The token gets validated by API-Server++ before executing your lambda, it has a default duration of 10 minutes and it can be configured via environment variable, in a Kubernetes-friendly way, in any case, authentication and authorization are transparent to your API and always enforced. All registered APIs are secure by default unless explicitly disabled, in this case, a clear message will be recorded in the logs when registering the API:
```
{"source":"server","level":"info","msg":"registered (insecure) WebAPI for path: /api/ping"}
```

### Testing with Javascript in the browser console

It's good to know how to test your APIs the "manual way" using CURL, but when passing the security token is required, it becomes a bit tedious, we can use a very simple HTML page with a bit of modern Javascript to automate API testing including security. For this exercise's sake we will assume that you are in your desktop environment, where you can use a browser to connect to the VM running your API, API-Server++ must be running on your Linux VM. 
Open the browser and navigate to this URL (PLEASE use your VM IP address or the hostname if you are using Canonical's Multipass VMs on Windows 10 Pro):
```
http://testvm.mshome.net:8080/api/sysinfo
```

Expected output on the browser page:
```
{"status": "OK", "data":[{"pod":"test","totalRequests":69,"avgTimePerRequest":0.00050976,"connections":2,"activeThreads":1}]}
```

There is another built-in API to serve metrics in a Grafana Prometheus-compatible format:
```
http://your_VM_address:8080/api/metrics
```

Expected output on the browser page:
```
# HELP cpp_requests_total The number of HTTP requests processed by this container.
# TYPE cpp_requests_total counter
cpp_requests_total{pod="test"} 70
# HELP cpp_connections Client tcp-ip connections.
# TYPE cpp_connections counter
cpp_connections{pod="test"} 2
# HELP cpp_active_threads Active threads.
# TYPE cpp_active_threads counter
cpp_active_threads{pod="test"} 1
# HELP cpp_avg_time Average request processing time in milliseconds.
# TYPE cpp_avg_time counter
cpp_avg_time{pod="test"} 0.00050264
```

Now that we verified that the connection to API-Server++ is OK, let's create an HTML file test.html on your disk, add this content and change the value of the _SERVER_ variable at the beginning of the `script` section, and save it:
```
<!doctype html>
	<head>
		<link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/@picocss/pico@1/css/pico.min.css">
		<title>JSON API tester</title>
	</head>

<html>
	<main class="container">
	<article>
		<h1>Please open the console with [Shift + Ctrl + i]</h1>
	</article>
	</main>
</html>

<script>
	// REMEMBER TO CHANGE THIS to point to your Ubuntu VM running API-Server++
	const _SERVER_ = "http://testvm.mshome.net:8080";

	onload = async function() {
		sessionStorage.setItem("token", ""); //no need for preflight on login
	
		//login
		const loginForm = new FormData();
		loginForm.append("username", "mcordova");
		loginForm.append("password", "basica");

		//call and wait for login to return
		await call_api("/api/login", function(json) {
				console.log("User: " + json.data[0].displayname);
				console.log("Token: " + json.data[0].id_token);
				sessionStorage.setItem("token", json.data[0].id_token); //store token for next request
			}, loginForm, postJSON = false);

		call_api("/api/shippers/view", function(json) {
					console.table(json.data); //print resultset to console
				});
	}

	async function call_api(uri, fn, formData, postJSON = false)
	{
		try {
			const token = sessionStorage.getItem("token");
			const auth = "Bearer " + sessionStorage.getItem("token");
			const headers = new Headers();
			if (token != "")
				headers.append("Authorization", auth);
			
			if (postJSON)
				headers.append("Content-type", "application/json");
			
			let options;
			if (formData === undefined)
				options = {method: 'GET', mode: 'cors', headers};
			else {
				if (postJSON)
					options = {method: 'POST', mode: 'cors', headers,  body: JSON.stringify(Object.fromEntries(formData))};
				else
					options = {method: 'POST', mode: 'cors', headers,  body: formData};
			}
			
			const res = await fetch(_SERVER_ + uri, options);
			if (res.ok) {
				const json = await res.json();
				console.log("TEST " + uri + " HTTP status: " + res.status + " JSON status: " + json.status);
				if (json.status == "OK") {
					fn(json);
				} else if (json.status == "EMPTY") {
					console.log("Data not found");
				} else if (json.status == "INVALID") {
					console.log("Validation error - description: " + json.validation.description + " id: " + json.validation.id);
				} else if (json.status == "ERROR") {
					console.log("Service error: " + json.description);
				}
			} else
				if (res.status == 401)
					console.log("Authentication required: please login");
				else
					console.log("HTTP error code: " + res.status);
		} catch (error) {
			console.log("Connection error: " + error.message);
		}
	}
</script>
```

Now double-click on the file to open it in the browser, and press `Shift + Ctrl + i` to open de developer tools, the console in particular, refresh the page several times and watch the results.
The `call_api()` function is a very handy testing tool that you can use as the base code to invoke your APIs or to use a page like this for quickly unit-testing your APIs, it is far less cumbersome than using CURL alone and you can take advantage of the browser's developer tools.

If you check you API-Server++ terminal you will see some log entries like these:
```
{"source":"security","level":"info","msg":"login OK - user: mcordova IP: 172.19.80.1 token: eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJsb2dpbiI6Im1jb3Jkb3ZhIiwibWFpbCI6ImNwcHNlcnZlckBtYXJ0aW5jb3Jkb3ZhLmNvbSIsInJvbGVzIjoiY2FuX2RlbGV0ZSwgY2FuX3VwZGF0ZSwgc3lzYWRtaW4iLCJleHAiOjE2OTE0NzM3MTl9.7z495wh6csYCavjxLK6-QIUWYWeFO2nLLQCI4gh44ts roles: can_delete, can_update, sysadmin","thread":"140455100950080"}
{"source":"access-log","level":"info","msg":"fd=13 remote-ip=172.19.80.1 POST path=/api/login elapsed-time=0.000907 user=","thread":"140455100950080"}
{"source":"access-log","level":"info","msg":"fd=13 remote-ip=172.19.80.1 OPTIONS path=/api/shippers/view elapsed-time=0.000007 user=","thread":"140455084164672"}
{"source":"access-log","level":"info","msg":"fd=10 remote-ip=172.19.80.1 OPTIONS path=/api/version elapsed-time=0.000004 user=","thread":"140455084164672"}
{"source":"access-log","level":"info","msg":"fd=13 remote-ip=172.19.80.1 OPTIONS path=/api/sysinfo elapsed-time=0.000006 user=","thread":"140455092557376"}
{"source":"access-log","level":"info","msg":"fd=14 remote-ip=172.19.80.1 GET path=/api/version elapsed-time=0.000007 user=","thread":"140455092557376"}
{"source":"access-log","level":"info","msg":"fd=10 remote-ip=172.19.80.1 GET path=/api/shippers/view elapsed-time=0.000601 user=mcordova","thread":"140455084164672"}
{"source":"access-log","level":"info","msg":"fd=13 remote-ip=172.19.80.1 GET path=/api/sysinfo elapsed-time=0.000008 user=","thread":"140455075771968"}
```

You can configure the log to be less verbose by changing the environment variables in the `run` script, which is recommended for production or during load tests because it has some overhead and the Ingress/Load Balancer will produce log entries like these anyway, there is no need to duplicate them.
```
export CPP_LOGIN_LOG=0
export CPP_HTTP_LOG=0
```
__Note__: warnings, errors, and custom log entries recorded by your APIs cannot be disabled.

## Retrieving a Master/Detail JSON document

In this example, we will use a set of tables to retrieve a customer as the master record and the customer's orders as an array of order records, a single stored procedure will encapsulate all the SQL logic and return the JSON document with all the data.

![image](https://github.com/cppservergit/apiserver-odbc/assets/126841556/278e6802-554c-441d-a2de-26c37a72bed5)

The returned JSON document looks like this, given a customer ID:
![image](https://github.com/cppservergit/apiserver-odbc/assets/126841556/734376a5-3e17-4370-a75c-e86e4e35ec72)

The DemoDB database contains the stored procedure `sp_customer_get` that returns the JSON response shown above, in a single roundtrip, isolating the API from the specific details of the data model, and also enhancing security by denying access to the data tables to API-Server++ login user, for security sake, a well-architected multi-tier system should never allow direct access to the data tables.
```
CREATE procedure [dbo].[sp_customer_get](@customerid varchar(10)) as
begin

	select  
		c.customerid, c.contactname, c.companyname, c.city, c.country, c.phone,
		orderid, 
		orderdate, 
		shipcountry, 
		orders.companyname as shipper, 
		total		
	from 
		customers c left join 
		(select orders.*, shippers.companyname, vw_order_totals.total
		from orders 
		inner join shippers on shippers.shipperid = orders.shipvia 
		inner join vw_order_totals on vw_order_totals.orderid = orders.orderid
		where orders.customerid = @customerid
		) orders on orders.customerid = c.customerid
		where c.customerid = @customerid
	order by c.customerid, orders.orderid
	for json auto, WITHOUT_ARRAY_WRAPPER	
	
end
```

This stored procedure is a bit more verbose than the SP used in the HelloWorld example, but the required code in API-Server++ is barely more complex, now we need to define an input parameter and its validation rules to pass to the stored procedure, and that's all, the interaction with the database is protected against SQL injection attacks by API-Server++'s inputs processor.

Stop the server with CTRL-C and edit main.cpp:
```
nano src/main.cpp
```

Add this code right above s.start()
```
	s.register_webapi
	(
		webapi_path("/api/customer/info"), 
		"Retrieve a customer main record and its related orders",
		http::verb::GET, 
		{ /* inputs */
			{"customerid", http::field_type::STRING, true}
		}, 	
		{} /* roles */,
		[](http::request& req)
		{
			auto sql {req.get_sql("sp_customer_get $customerid")};
			req.response.set_body(sql::get_json_response("DB1", sql));
		}
	);
```
As you can see, this API expects an input parameter named `customerid`, of type `STRING` and it is required, it MUST be included in the invocation as a URI parameter, something like:

```
http://YouServer:8080/api/customer/info?customerid=BOLID
```

The whole program should look like this:
```
#include "server.h"

int main()
{
	server s;
        s.register_webapi
        (
                webapi_path("/api/shippers/view"),
                "List of shipping companies",
                http::verb::GET,
                {} /* inputs */,
                {} /* roles */,
                [](http::request& req)
                {
                        req.response.set_body(sql::get_json_response("DB1", "sp_shipper_view"));
                }
        );

	s.register_webapi
	(
		webapi_path("/api/customer/info"), 
		"Retrieve a customer main record and its related orders",
		http::verb::GET, 
		{ /* inputs */
			{"customerid", http::field_type::STRING, true}
		}, 	
		{} /* roles */,
		[](http::request& req)
		{
			auto sql {req.get_sql("sp_customer_get $customerid")};
			req.response.set_body(sql::get_json_response("DB1", sql));
		}
	);

        s.start();
}
```

CTRL-X to save and exit. Recompile:
```
make
```

Expected output:
```
g++-13 -Wno-unused-parameter -Wpedantic -Wall -Wextra -O3 -std=c++23 -pthread -flto=4 -fno-extern-tls-init -march=x86-64 -mtune=intel -I/usr/include/postgresql -c src/main.cpp
g++-13 -Wno-unused-parameter -Wpedantic -Wall -Wextra -O3 -std=c++23 -pthread -flto=4 -fno-extern-tls-init -march=x86-64 -mtune=intel  env.o logger.o jwt.o httputils.o email.o odbcutil.o sql.o login.o util.o main.o -lodbc -lcurl -lcrypto -o "apiserver"
```

Run the new version:
```
./run
```

The program log should contain these lines at the beginning:
```
{"source":"signal","level":"info","msg":"signal interceptor registered"}
{"source":"server","level":"info","msg":"registered WebAPI for path: /api/shippers/view"}
{"source":"server","level":"info","msg":"registered WebAPI for path: /api/customer/info"}
```

You new API has been registered and is ready for testing, you can use your HTML page, test.html, just add these lines to the tester code:
```
		call_api("/api/customer/info?customerid=BOLID", function(json) {
					console.log(JSON.stringify(json));
				});
```
The response is the same JSON document shown above

## Additional API Examples

The DemoDB database contains many stored procedures that return JSON and can be used to create APIs, but also stored procedures that modify data (insert/update/delete) and won't return any results. There is an example of main.cpp with all the API definitions for this database, as well as an HTML5/CCS3 web-responsive frontend to consume these APIs, more on this later. In the following sections, you will find different types of Web API definitions, using more features than the examples above.

### Invoke stored procedure to insert or update record

```
	s.register_webapi
	(
		webapi_path("/api/gasto/add"), 
		"Add expenses record",
		http::verb::POST, 
		{
			{"fecha", http::field_type::DATE, true},
			{"categ_id", http::field_type::INTEGER, true},
			{"monto", http::field_type::DOUBLE, true},
			{"motivo", http::field_type::STRING, true}			
		},
		{},
		[](http::request& req) 
		{
			auto sql {req.get_sql("sp_gasto_insert $fecha, $categ_id, $monto, $motivo")};
			sql::exec_sql("DB1", sql);
			req.response.set_body(R"({"status":"OK"})");
		}
	);
```
Input rules are defined for each field, the name, the data type expected, and if it is required or optional, the name will be used to automatically replace the value in the SQL template when using the `req.get_sql()` function, API-Server++ takes care of pre-processing the fields to ensure that no SQL-injection attacks pass thru, so they can be safely replaced into the SQL template. A multipart form POST is the only verb accepted for this API. With this definition of the API, the Server will take care of processing the request and validating the inputs as well as the security (authentication/authorization if roles were defined), when all the preconditions are met, then the lambda function will be executed. You can add your custom validation code inside the lambda function right before the execution of the SP, to add constraints to your API contract, so to speak. When the API executes a procedure that modifies data and does not return any resultsets, then a minimal JSON response with OK status is all that needs to be returned, as shown above. Also in this case the function `sql::exec_sql()` is used to execute a stored procedure that does not return a resultset.

The case for using a procedure that updates a record is very similar, but in this case, we used the roles field to set authorization restrictions, only users with the specified roles (can_update) can invoke this Web API:
```
	s.register_webapi
	(
		webapi_path("/api/gasto/update"), 
		"Update expense record",
		http::verb::POST, 
		{
			{"gasto_id", http::field_type::INTEGER, true},
			{"fecha", http::field_type::DATE, true},
			{"categ_id", http::field_type::INTEGER, true},
			{"monto", http::field_type::DOUBLE, true},
			{"motivo", http::field_type::STRING, true}			
		},
		{"can_update"},
		[](http::request& req) 
		{
			auto sql {req.get_sql("sp_gasto_update $gasto_id, $fecha, $categ_id, $monto, $motivo")};
			sql::exec_sql("DB1", sql);
			req.response.set_body(R"({"status":"OK"})");
		}
	);
```
If authorization fails then a JSON with the status "INVALID" will be returned and a description, so the client can react to this case.
```
{
    "status": "INVALID",
    "validation": {
        "id": "_dialog_",
        "description": "err.accessdenied"
    }
}
```
The stored procedure that serves as the backend to this Web API:
```
create procedure sp_gasto_update (@gasto_id int, @fecha as date, @categ_id as int, @monto as float, @motivo as varchar(100)) as
begin

		UPDATE gasto SET
			fecha=@fecha,
			categ_id=@categ_id,
			monto=@monto,
			motivo=@motivo
		WHERE
			gasto_id=@gasto_id
			
end
```

### Search filter API

This API executes a stored procedure that returns a JSON response to fill a sales report, sales by category between two dates, the date-from/date-to parameters are the input rules for this API:
```
	s.register_webapi
	(
		webapi_path("/api/sales/query"), 
		"Sales report by category in a time interval",
		http::verb::POST, 
		{
			{"date1", http::field_type::DATE, true},
			{"date2", http::field_type::DATE, true}
		},
		{"report", "sysadmin"},
		[](http::request& req) 
		{
			auto sql {req.get_sql("sp_sales_by_category $date1, $date2")};
			std::string json {sql::get_json_response("DB1", sql)};
			req.response.set_body(json);
		}
	);
```
API-Server++ will enforce both security (authentication and authorization) and input validation rules automatically.

This is an HTML5 representation of the frontend for this API:
![image](https://github.com/cppservergit/apiserver-odbc/assets/126841556/d22b33cb-d7fd-459f-8653-e1a884e47722)

The JSON response:
```
{
    "status": "OK",
    "data": [
        {
            "id": 1,
            "item": "Beverages",
            "subtotal": 267868.18
        },
        {
            "id": 4,
            "item": "Dairy Products",
            "subtotal": 234507.285
        },
        {
            "id": 3,
            "item": "Confections",
            "subtotal": 167357.225
        },
        {
            "id": 6,
            "item": "Meat/Poultry",
            "subtotal": 163022.3595
        },
        {
            "id": 8,
            "item": "Seafood",
            "subtotal": 131261.7375
        },
        {
            "id": 2,
            "item": "Condiments",
            "subtotal": 106047.085
        },
        {
            "id": 7,
            "item": "Produce",
            "subtotal": 99984.58
        },
        {
            "id": 5,
            "item": "Grains/Cereals",
            "subtotal": 95744.5875
        }
    ]
}
```
The backend stored procedure may be of a certain SQL complexity, as in this case, but this is hidden from the API implementation, as long as it returns JSON, we are good.
In this example we also used a list of authorized roles `{"report", "sysadmin"}`, the user invoking the API must belong to any of those roles, otherwise, execution will be denied and a JSON response with the status INVALID will be returned as shown above for the `/api/gasto/update` example, on the server side a detailed log record will be generated by API-Server++ for security audit purposes:
```
{"source":"service","level":"error","msg":"/api/sales/query, Access denied for user: mbencomo from IP: 172.27.160.1 reason: User roles are not authorized to execute this service: general","thread":"140221297444544","x-request-id":""}
```
__NOTE__: If you want to test this API, use dates between 1994-01-01 and 1996-12-31.

### Delete record API with additional custom validator

This API will invoke a stored procedure to delete a record, but instead of waiting for the database to raise an error if there is a violation of referential integrity, the API implements a custom validator rule using a lambda inside the main function body, this way an INVALID status with a specific user-oriented message may be returned instead of an ERROR:
```
	s.register_webapi
	(
		webapi_path("/api/categ/delete"), 
		"Delete category record",
		http::verb::GET, 
		{{"id", http::field_type::INTEGER, true}},
		{"can_delete"},
		[](http::request& req) 
		{
			//validator for referential integrity
			req.enforce("_dialog_", "$err.delete", [&req]()-> bool { 
				return !sql::has_rows("DB1", req.get_sql("sp_categ_in_use $id"));
			});
			sql::exec_sql("DB1", req.get_sql("sp_categ_delete $id"));
			req.response.set_body(R"({"status":"OK"})");
		}
	);
```
The `req.enforce()` method evaluates the result of the passed lambda function (validator), if false then stops execution of the API and the client will receive a JSON response with status INVALID and the fields passed to this method (first two arguments). In this example, the validator checks if this category ID is being used in another table, the SQL logic for this is encapsulated in `sp_categ_in_use`, which does not return JSON but a regular resultset, the function `sql::has_rows()` returns true if the resultset contains at least 1 row. This example shows how custom validation/pre-condition rules can be applied inside an API, and if any of these custom validators return false then the rest of the code won't be executed, that's the guarantee enforced by API-Server++.

The backend SP behind the validator is very simple and optimized for a quick response:
```
CREATE procedure sp_categ_in_use(@id int) as
begin
	set nocount on
	select top 1 gasto_id from gasto where categ_id = @id
end
```

### Sending email

API-Server++ uses libcurl to send mail with secure SMTP (TLS), this has been tested with GMail's server using a google workspace account. To use this feature you have to configure the corresponding environment variables with correct values, edit the `run` bash script and change all the variables starting with `CPP_MAIL_*`:
```
# secure mail config
export CPP_MAIL_SERVER="smtp://smtp.gmail.com:587"
export CPP_MAIL_USER="admin@martincordova.com"
export CPP_MAIL_PWD="your-smtp-password"
```

A new thread will be used to send the email, this way your function returns immediately without blocking the thread that's executing your API function, and the server can send the response to the client and keep processing requests. Any error returned by `libcurl` will be recorded in the logs. It's recommended practice to invoke the `send_mail()` function after executing your database I/O and the `set_body()` function. If any error occurs doing I/O, the `send_mail()` will never be called. Mail delivery errors will be probably notified to the sender address, that is the address configured in the `CPP_MAIL_USER` environment variable.

The http::request class provides several functions to send mail with more or fewer arguments (CC, attachments, etc) to make the code simpler whenever possible.

Example of simple invocation without CC and no attachment:
```
	server s;
	s.register_webapi
	(
		webapi_path("/api/gasto/add"), 
		"Add expense record",
		http::verb::POST, 
		{
			{"fecha", http::field_type::DATE, true},
			{"categ_id", http::field_type::INTEGER, true},
			{"monto", http::field_type::DOUBLE, true},
			{"motivo", http::field_type::STRING, true}			
		},
		{},
		[](http::request& req) 
		{
			sql::exec_sql("DB1", req.get_sql("sp_gasto_insert $fecha, $categ_id, $monto, $motivo"));
			req.response.set_body(R"({"status":"OK"})");
			req.send_mail(
					req.user_info.mail, //TO
					"Document uploaded via API-Server++",
					"expenditure-msg.html"
				);			
		}
	);
```
You can obtain the currently logged-in user's email using `req.user_info.mail`. The body of the message must be an HTML document that you provide, it must be stored in `/var/mail`, and it can contain parameter markers corresponding to the input fields, these are used like in the SQL templates: $fieldname. The `send_mail()` function takes care of loading the template and injecting input fields if necessary.

There is a variant of the `send_mail()` function that accepts a CC argument, and also another one with a single attachment:
```
void send_mail(const std::string& to, const std::string& cc, const std::string& subject, const std::string& body);
void send_mail(const std::string& to, const std::string& cc, const std::string& subject, const std::string& body, const std::string& attachment, const std::string& attachment_filename);
```

The code below sends an email with an attachment:
```
			req.send_mail(
					req.user_info.mail, //TO
					"cppserver@martincordova.com", //CC - can be empty
					"Document uploaded via API-Server++",
					"upload-msg.html",
					req.get_param("document"), //attachment
					req.get_param("filename") //original filename
				);	
```
In this particular example, a file was uploaded using API-Server++ automatic upload facility, it does store the file with a unique UUID name in a storage area using the path `/var/blobs`, on Kubernetes this is only a path mapped to a shared storage service, when running native on Linux this may be the actual directory or an NFS mount.
The `req.get_param("document")` call returns the UUID name of the uploaded file, we also provide the original filename so the attachment will be properly named inside the mail. If we use an absolute path like "/mydir/myfile.pdf" then the `send_mail` function won't assume this is a blob, it will try to load the file from the path provided and the last parameter can be passed as an empty string `""`.
The example above was tailored to the case of blob uploads, where files are stored in a directory mapped to `/var/blobs` using an auto-generated UUID as the file name and the rest of the parameters are stored in a table using a stored procedure, you may want to create a sort of feedback sending an email notifying the occurrence of the upload, the uploaded file and its basic information (title, size, etc).

## Demo App

There is a complete Demo case, frontend, and backend, you will need TestDB and DemoDB to run it:

* [Demo Web Responsive App](https://cppserver.com/files/apiserver/demo.zip)
* [main.cpp](https://cppserver.com/files/apiserver/main.cpp)

Instructions:

1) Backend: download main.cpp into /apiserver/src and recompile with `make`. From the directory /apiserver-odbc, make sure the server is not running and then execute:
```
curl https://cppserver.com/files/apiserver/main.cpp -o src/main.cpp
make
```

2) Frontend: unzip demo.zip, this is a modern and lightweight demo HTML5 WebApp, edit /www/js/utils.js to point the _SERVER_ variable to your API-Server++ and double-click on index.html, log in with user mcordova/basica and play with it. This is a responsive webapp with several cool features. You don't need a web server to access this static website, the browser can use it straight from the filesystem.

![ui](https://github.com/cppservergit/apiserver/assets/126841556/36b7910d-937e-45d1-a4b4-f5748a90cbb0)

### Uploads

For testing the upload feature you need to create /var/blobs on your VM and assign permissions so API-Server++ can read/write files into that directory:
```
sudo mkdir /var/blobs
sudo chmod 777 /var/blobs
```

When using API-Server++ as a container on Kubernetes, volumes and volume mappings will be used to map /var/blobs to the actual storage destination on the Kubernetes Cluster. This is transparent to API-Server++.

## Memory safety

API-Sever++ has been tested for memory safety (leaks and overflows) with dynamic analysis instrumentation tools including Valgrind and GCC memory sanitizer (-fsanitize=leak and -fsanitize=address), It has passed all tests, with no leaks or warning of any sort when running a load of 2000 concurrent connections executing a variety of API requests involving database operations as well as diagnostics.

Valgrind report (GCC sanitizers only print if problems are found):

```
==3412== HEAP SUMMARY:
==3412==     in use at exit: 0 bytes in 0 blocks
==3412==   total heap usage: 4,423,921 allocs, 4,423,921 frees, 1,453,005,010 bytes allocated
==3412==
==3412== All heap blocks were freed -- no leaks are possible
==3412==
==3412== For lists of detected and suppressed errors, rerun with: -s
==3412== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
```

API-Server++ is a small program, code coverage is pretty complete with these tests, and most probable code paths, if not all, are executed.
Please note that in order to use dynamic analysis tools you need to compile with `-g` and `-O0`.

## Static analysis with SonarCloud

SonarCloud is the top player in C++ static analysis, performing rigorous analysis of the code to ensure compliance with [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) as well as a database of Sonar C++ Rules, it is really strict, API-Server++ was analyzed and rewritten in order to achieve a top score with SonarCloud, this way we can provide a quality Modern C++ code-base that implements the industry-accepted best practices, and at the same time is simple and fast.

![image](https://github.com/cppservergit/apiserver/assets/126841556/8422507e-f30a-44dd-8539-1c6af62402b1)

We managed to fix all issues reported and the current code base has achieved a perfect score for all the code, no issues of any kind were detected.

## Static analysis with open-source tools

API-Sever++ has been tested with CPPCheck and FlawFinder, it has passed all tests, FlawFinder prints 3 warnings that can be safely assumed as false positives:

```
FINAL RESULTS:

src/env.cpp:26:  [3] (buffer) getenv:
  Environment variables are untrustable input if they can be set by an
  attacker. They can have any content and length, and the same variable can
  be set more than once (CWE-807, CWE-20). Check environment variables
  carefully before using them.
src/env.cpp:42:  [3] (buffer) getenv:
  Environment variables are untrustable input if they can be set by an
  attacker. They can have any content and length, and the same variable can
  be set more than once (CWE-807, CWE-20). Check environment variables
  carefully before using them.
src/server.cpp:455:  [1] (buffer) read:
  Check buffer boundaries if used in a loop including recursive loops
  (CWE-120, CWE-20).
```

The first two mention code related to reading environment variables, which in the case of API-Server++ is an unavoidable task and is always supplied by a system administrator, when using Kubernetes some of them would be Secrets, so they are safe to read, and the code used to read the variables takes care of safe type conversion and `nullptr` checks, example:

```
	unsigned short int env_vars::read_env(const char* name, unsigned short int default_value) noexcept
	{
		unsigned short int value{default_value};
		if (const char* env_p = std::getenv(name)) {
			std::string_view str(env_p);
			auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);
			if (ec == std::errc::invalid_argument)
				logger::log(LOGGER_SRC, "warn", std::string(__FUNCTION__) + " -> invalid argument for std::from_chars: " + std::string(env_p) + " env-var: " + std::string(name), true);
			else if (ec == std::errc::result_out_of_range)
				logger::log(LOGGER_SRC, "warn", std::string(__FUNCTION__) + " -> number out of range in std::from_chars: " + std::string(env_p) + " env-var: " + std::string(name), true);
		}
		return value;
	}
```
The code above will not accept an invalid value, if one was supplied via the environment, then it will return the default value. Modern C++ is used to achieve this.

The last one is related to the use of the read() C function used to read from data from sockets, in this case, a C++ std::array type is used for the buffer, and there is no way a buffer overflow can happen:
```
int count = read(fd, data.data(), data.size());
```
It won't read more bytes than `data.size()`.

Therefore we can assume these warnings are not relevant and the program is memory-safe. 
For the dynamic runtime instrumentation, the program was bombarded with 2000 concurrent connections and thousands of requests, so all code was covered (it's a simple program, not many paths of execution), and no leaks were found.

## API-Server++ for ODBC

There is a separate branch of this project that instead of using the native PostgreSQL driver the server does use the ODBC API. This way you can connect to SQL Server, Sybase, or DB2 using their native ODBC drivers. Please note that in the case of SQL Server, its ODBC driver is the native client.

### Pre-requisites

For development purposes please install these packages:
```
sudo apt install g++-13 libssl-dev libpq-dev libcurl4-openssl-dev uuid-dev libjson-c-dev libldap-dev unixodbc-dev tdsodbc make -y --no-install-recommends
```
This command will also install [FreeTDS](https://www.freetds.org/index.html) ODBC driver for SQL Server and Sybase.

### Register FreeTDS driver for SQLServer and Sybase

Edit odbcinst.ini
```
sudo nano /etc/odbcinst.ini
```

Add this content:
```
[FreeTDS]
Driver=/usr/lib/x86_64-linux-gnu/odbc/libtdsodbc.so
UsageCount=1
```
CTRL-X + Y to save.

### Retrieve ODBC branch from GitHub

If you are already using the PgSQL version of API-Server++ please use another directory to clone the ODBC branch, it's basically the same project but with two different modules, sql and login.
```
git clone -b odbc https://github.com/cppservergit/apiserver.git
```

To build is the same procedure:
```
cd apiserver
```

Compile:
```
make
```

### Implementing the security tables in SQL Server

In order to use the basic login adapter included with API-Server++ you will need some tables and a stored procedure, you can use a new database `testdb` for this purpose, or create them on any existing database.

![image](https://github.com/cppservergit/apiserver/assets/126841556/1dcbaeae-6881-457c-89b3-6cd207d3148f)

The underlying tables structure is not relevant to API-Server++, it only requires the existence of a stored procedure `cpp_dblogin( @userlogin varchar(100), @userpassword varchar(100) )` that will return a resultset with 1 row and 3 columns if the login was successful: mail, displayname and rolenames, this last field should contain the roles of the user separated by ",". We provide an example of such a stored procedure for a very basic security database model, you can find its definition at the end of the script.

```
CREATE TABLE testdb.dbo.s_role (
	role_id int IDENTITY(1,1) NOT NULL,
	rolename varchar(100) COLLATE SQL_Latin1_General_CP1_CI_AS NOT NULL,
	CONSTRAINT PK__s_role__CD994BF27B3DC626 PRIMARY KEY (role_id)
);

CREATE TABLE testdb.dbo.s_user (
	user_id int IDENTITY(1,1) NOT NULL,
	userlogin varchar(100) COLLATE SQL_Latin1_General_CP1_CI_AS NOT NULL,
	passwd varchar(100) COLLATE SQL_Latin1_General_CP1_CI_AS NOT NULL,
	displayname varchar(100) COLLATE SQL_Latin1_General_CP1_CI_AS NOT NULL,
	mail varchar(100) COLLATE SQL_Latin1_General_CP1_CI_AS NOT NULL,
	CONSTRAINT PK__s_user__CBA1B25775A0F870 PRIMARY KEY (user_id),
	CONSTRAINT s_user_UN UNIQUE (userlogin)
);

CREATE TABLE testdb.dbo.s_user_role (
	user_role_id int IDENTITY(1,1) NOT NULL,
	user_id int NOT NULL,
	role_id int NOT NULL,
	CONSTRAINT PK__s_user_r__CD994BF2DA948C68 PRIMARY KEY (role_id),
	CONSTRAINT s_user_role_FK FOREIGN KEY (user_id) REFERENCES testdb.dbo.s_user(user_id) ON DELETE CASCADE,
	CONSTRAINT s_user_role_FK2 FOREIGN KEY (role_id) REFERENCES testdb.dbo.s_role(role_id) ON DELETE CASCADE
);

INSERT INTO testdb.dbo.s_user
           (userlogin, passwd, displayname, mail)
     VALUES
		('mcordova', 'sY1Y5lZMlyG12Kr0P4qQXOv2H50ycI63kft1e4pmoR4=', 'Martín Córdova', 'cppserver@martincordova.com'),
        ('mbencomo', 'BiumdlzR0xeh4VUSNmHkBXn5sFLnFG0VDBx/JcJsc5Y=', 'María Eugenia Bencomo', 'cppserver@martincordova.com');

INSERT INTO testdb.dbo.s_role (rolename) VALUES
	 ('sysadmin'),
	 ('can_delete'),
	 ('can_update'),
	 ('general');
	 
INSERT INTO testdb.dbo.s_user_role (user_id,role_id) VALUES
	 (1,1),
	 (1,2),
	 (1,3),
	 (2,4);

CREATE procedure [dbo].[cpp_dblogin] ( @userlogin varchar(100), @userpassword varchar(100) ) as
begin

	DECLARE @pwd varchar(200);
	DECLARE @encodedpwd varchar(200);
	set @pwd = @userlogin + ':' + @userpassword;
	
	select distinct STRING_AGG(r.rolename, ' ') as rolenames into #roles from testdb.dbo.s_user_role ur inner join testdb.dbo.s_user u
	on u.user_id = ur.user_id
	inner join testdb.dbo.s_role r on r.role_id = ur.role_id
	where u.userlogin = @userlogin;
	
	SET @encodedpwd = (select hashbytes('SHA2_256', @pwd) FOR XML PATH(''), BINARY BASE64);
	set nocount on
	
	select mail as email, displayname, rolenames from testdb.dbo.s_user WITH (NOLOCK), #roles where
	userlogin = @userlogin and passwd = @encodedpwd

end
;
```
The SQL objects shown above are equivalent to the PostgreSQL `TestDB` sample database.

### The login module

The `login.cpp` module in the odbc branch is almost the same as the one in the master (PostgreSQL) branch, only the SQL for calling the stored procedure CPP_DBLOGIN changes. The modules `sql.h` and `sql.cpp` are specific for this ODBC branch, but the API is very similar to those in the master branch, in fact the ODBC module is a superset, with very few additional functions declared in `sql.h` and implemented in `sql.cpp`, the interface defined in the namespace sql:: is basically the same for both branches, the code implementing WebAPIs won't "feel" the difference.

```
namespace login
{
	struct login_result
	{
		public:
			login_result(bool _result, const std::string& _name, const std::string& _mail,const std::string& _roles) noexcept;
			std::string get_email() const noexcept;
			std::string get_display_name() const noexcept;
			std::string get_roles() const noexcept;
			bool ok() const noexcept;
		private:
			bool result;
			std::string display_name;
			std::string email;
			std::string roles;
	};	
	login_result bind(const std::string& login, const std::string& password);
}
```
Any variant of the login mechanism, like `loginldap` module, must implement the same interface.

In the case of `login.cpp` its current implementation is very simple, it depends on the expected behavior of the `cpp_dblogin` stored procedure:
```
	//login and password must be pre-processed for sql-injection protection
	//expects a resultset with these columns: mail, displayname, rolenames
	login_result bind(const std::string& login, const std::string& password)
	{
		std::string sql {std::format("execute cpp_dblogin '{}', '{}'", login, password)};
		if (auto rec {sql::get_record("CPP_LOGINDB", sql)}; !rec.empty()) {
			return login_result {true, rec["displayname"], rec["email"], rec["rolenames"]};
		} else
			return login_result{false, "", "", ""};
	}
```

The implementation of JWT (JSON Web Token) and the mechanism of checking authentication and authorization depend on the correct implementation of the login interface.

#### Custom login implementations

It is possible to change the implementation of the `bind()` function if you are not using hashed passwords that can be generated via SQL functions (like in this default implementation), maybe you are using BCrypt or something similar, in any case, the modifications are simple and we can provide support, just open an issue on GitHub. In the specific case of BCrypt, we already have a login implementation using a C-compiled library for BCrypt for x86-64.

### Retrieving JSON

There are very few differences between the ODBC and the PgSQL version of API-Server++, mainly in the `sql` module, because PgSQL is very powerful returning JSON straight from the database, and API-Server++ takes advantage of that. With the ODBC version the `sql` module has the same interface, but in the implementation, it retrieves the resultset from the database and proceeds to assemble the JSON response in memory, there is also an overload of the `sql::get_json_response()` for the cases when multiple resultsets are returned, here is an example that returns customer and orders in a single JSON response:

```
	server::register_webapi
	(
		server::webapi_path("/api/customer/info"), 
		"Retrieve customer record and the list of his purchase orders",
		http::verb::GET, 
		{{"customerid", http::field_type::STRING, true}}, 	
		{"customer_access", "sysadmin"},
		[](http::request& req)
		{
			auto sql {req.get_sql("execute sp_customer_info $customerid")};
			req.response.set_body(sql::get_json_response("DB1", sql, {"customer", "orders"}));
		}
	);
```

A stored procedure that returns a resultset looks like this:
```
CREATE procedure [dbo].[sp_shippers_view] as
begin
	set nocount on
		SELECT 
			shipperid,
			companyname,
			phone
		FROM 
			shippers WITH (NOLOCK)
end
```
The `set nocount on` is important.

### Running API-Server++ and connecting via ODBC

In the apiserver directory, make the script executable
```
chmod +x run
```

Edit the run script
```
nano run
```

Locate these lines and change attributes according to your environment:
```
# ODBC SQL authenticator config
export CPP_LOGINDB="Driver=FreeTDS;SERVER=demodb.mshome.net;PORT=1433;DATABASE=testdb;UID=sa;PWD=basica;APP=CPPServer;Encryption=off;ClientCharset=UTF-8"
# ODBC SQL data sources
export DB1="Driver=FreeTDS;SERVER=demodb.mshome.net;PORT=1433;DATABASE=demodb;UID=sa;PWD=basica;APP=CPPServer;Encryption=off;ClientCharset=UTF-8"
```
CPP_LOGINDB is the database where the security tables and the stored procedure `cpp_dblogin` reside.

## Differences between PostgreSQL and ODBC versions

PostgreSQL SQL functions return JSON from queries, except for specific cases that return a single record as a resultset, when using ODBC (SQL Server/DB2) the stored procedures will return resultsets, API-Server++ ODBC can only invoke stored procedures that return resultsets or return nothing, it does not support output parameters, only resultsets. For the PgSQL version, the rule is to return JSON, a single-row resultset, or return nothing, only SQL functions return JSON or resultsets in PgSQL, stored procedures can't return resultsets in PgSQL unlike SQL Server or DB2.
