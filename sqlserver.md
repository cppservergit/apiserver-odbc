# QuickStart SQL Server with docker on Ubuntu 22.04

Let's assume that you have a brand-new Ubuntu 22.04 virtual machine, if it was created with multipass you will use the `ubuntu` user and its `/home/ubuntu` directory.
Please login to this VM and make sure you are in your home directory.

When using multipass you can create it with this command:
```
multipass launch -n sqlserver -c 4 -m 4G -d 12G
multipass shell sqlserver
```
These parameters are enough to run a few small demo databases. Add more disk space if necessary.

## Step 1: Install tree
This software will let you see the full directory structure and verify that SQL Server filled it with its data/log files.
```
sudo apt update && sudo apt install tree
```

## Step 2: Install latest Docker engine
```
curl -fsSL https://get.docker.com -o get-docker.sh && sh get-docker.sh && sudo sh get-docker.sh
```

## Step 3: Pull SQL Server 2019 image for docker
This may take a few minutes, it is a large image.
```
sudo docker pull mcr.microsoft.com/mssql/server:2019-latest
```

## Step 4: Create a directory structure for SQL Server data files
```
mkdir sql && mkdir sql/data && mkdir sql/log && mkdir sql/secrets
chmod -R 777 sql
```

## Step 5: Run SQL Server container
You may change the value of MSSQL_SA_PASSWORD, do not touch the rest of the command, it is a single line.
```
sudo docker run -d --restart unless-stopped --name mssql --network host -v ./sql/data:/var/opt/mssql/data -v ./sql/log:/var/opt/mssql/log -v ./sql/secrets:/var/opt/mssql/secrets -e "ACCEPT_EULA=Y" -e "MSSQL_PID=Developer" -e "MSSQL_SA_PASSWORD=Basica2024" mcr.microsoft.com/mssql/server:2019-latest
```
Wait a few seconds then proceed to next step.

## Step 6: Verify that SQL Server created the basic data files.
```
tree sql
```

You should see something like this:
```
sql
├── data
│   ├── Entropy.bin
│   ├── master.mdf
│   ├── mastlog.ldf
│   ├── model.mdf
│   ├── model_msdbdata.mdf
│   ├── model_msdblog.ldf
│   ├── model_replicatedmaster.ldf
│   ├── model_replicatedmaster.mdf
│   ├── modellog.ldf
│   ├── msdbdata.mdf
│   ├── msdblog.ldf
│   ├── tempdb.mdf
│   ├── tempdb2.ndf
│   ├── tempdb3.ndf
│   ├── tempdb4.ndf
│   ├── templog.ldf
├── log
│   ├── HkEngineEventFile_0_133550319560340000.xel
│   ├── errorlog
│   ├── errorlog.1
│   ├── log.trc
│   ├── sqlagentstartup.log
│   └── system_health_0_133550319567040000.xel
└── secrets
    └── machine-key
```

## Step 7: Download API-Server demo database backups
```
curl https://cppserver.com/files/apiserver/demodb.bak -O
curl https://cppserver.com/files/apiserver/testdb.bak -O

```

## Step 8: Copy the backups to the data directory of the container
```
cp *.bak sql/data
```

## Step 9: Restore the backups

### demodb
```
sudo docker exec -it mssql /opt/mssql-tools/bin/sqlcmd -S localhost \
   -U SA -P 'Basica2024' \
   -Q 'RESTORE DATABASE demodb FROM DISK="/var/opt/mssql/data/demodb.bak" WITH REPLACE, RECOVERY;'
```

### testdb
This is the security database that serves as an example of integration with a custom SQL-based security mechanism (users, roles, etc), of particular importance is the stored procedure `cpp_dblogin`, regardless of the security database structure, API-Server++ login module expects an SP with this name, parameters, and other conventions; the SP is fully documented inside.
```
sudo docker exec -it mssql /opt/mssql-tools/bin/sqlcmd -S localhost \
   -U SA -P 'Basica2024' \
   -Q 'RESTORE DATABASE testdb FROM DISK="/var/opt/mssql/data/testdb.bak" WITH REPLACE, RECOVERY;'
```

That's it, your SQL Server should be ready to accept connections.

## Notes

### ODBC connection strings
Take note of your VM IP address with `ip a` looking for the 2nd or 3rd interface, if you are using more than one bridge, as would be the case if you combine multipass with an LXD bridge when using only multipass then it should be the 2nd interface, usually this will be the same for a VM created with another Ubuntu virtualization software:
```
Driver=FreeTDS;SERVER=172.22.103.242;PORT=1433;DATABASE=testdb;UID=sa;PWD=Basica2024;APP=API-Server;Encryption=off;ClientCharset=UTF-8
Driver=FreeTDS;SERVER=172.22.103.242;PORT=1433;DATABASE=demodb;UID=sa;PWD=Basica2024;APP=API-Server;Encryption=off;ClientCharset=UTF-8
```
