#FROM node:latest
FROM arm32v6/alpine:3.7
# Surpress Upstart errors/warning
RUN dpkg-divert --local --rename --add /sbin/initctl
RUN ln -sf /bin/true /sbin/initctl

# Let the container know that there is no tty
ENV DEBIAN_FRONTEND noninteractive

# Install software requirements
RUN apt-get update && \
apt-get install -y git
RUN npm install -g node-gyp

#-- C++ Install
RUN apt-get install g++

# Clone the conf files into the docker container
RUN git clone https://github.com/Eba-M/E3DC-Control.git /home/e3dc-control

# run start script
CMD ["/bin/bash", "cd /home/e3dc-control"]
CMD ["/bin/bash", "/home/e3dc-control/make"]
CMD ["/bin/bash", "/home/e3dc-control/E3DC_CONTROL"]
