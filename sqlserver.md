# QuickStart SQL Server with docker on Ubuntu 22.04

Let's assume that you have a brand-new Ubuntu 22.04 virtual machine, if it was created with multipass you will use the `ubuntu` user and its `/home/ubuntu` directory.
Please login to this VM.

When using multipass you can create it with this command:
```
multipass launch -n sqlserver -c 4 -m 4G -d 12G
```
These parameters are enough to run a few small demo databases. Add more disk space if necessary.

## Step 1: Install tree
This software will let you see directory structure and verity that SQL Server filled it with its own data/log files.
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

## Step 3: Pull SQL Server 2019 image for docker
This may take a few minutes, it is a large image.
```
sudo docker pull mcr.microsoft.com/mssql/server:2019-latest
```
