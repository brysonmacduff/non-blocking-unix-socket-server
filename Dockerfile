FROM ubuntu:22.04

RUN apt update -y
RUN apt install -y build-essential

RUN mkdir -p /usr/local/bin
COPY build/server/server ./usr/local/bin/server
COPY build/client/client ./usr/local/bin/client

ENTRYPOINT [ "/bin/bash" ]