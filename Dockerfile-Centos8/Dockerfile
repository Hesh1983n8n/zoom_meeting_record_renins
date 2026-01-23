# Use the CentOS 9 base image
FROM quay.io/centos/centos:8

# Set the working directory
WORKDIR /etc/yum.repos.d/
RUN sed -i 's/mirrorlist/#mirrorlist/g' /etc/yum.repos.d/CentOS-*
RUN sed -i 's|#baseurl=http://mirror.centos.org|baseurl=http://vault.centos.org|g' /etc/yum.repos.d/CentOS-*

RUN yum update -y

# Install required packages
RUN yum install -y dnf-plugins-core \
    && dnf install -y cmake gcc gcc-c++ \
    && dnf config-manager --set-enabled powertools \
    && dnf install -y epel-release epel-next-release \
    && yum install -y SDL2-devel mesa-libGL mesa-libGL-devel mesa-dri-drivers \
    && yum install -y libxcb-devel xcb-util-image xcb-util-keysyms \
    && yum install -y openssl-devel \
    && yum install -y libcurl-devel \
    && yum clean all

RUN yum install -y glib2-devel

RUN yum install -y pulseaudio pulseaudio-utils

 # Set the working directory
WORKDIR /app

 # Copy your application files to the container
COPY demo/ /app/demo/

# Execute additional commands
 RUN cd /app/demo && rm -rf bin && rm -rf build && cmake -B build && cd build && make

 # Set the working directory to the binary folder
 WORKDIR /app/demo/bin

# # Define a shell script to run multiple commands
 RUN echo '#!/bin/bash' > /app/demo/run.sh \
     && echo '/app/demo/setup-pulseaudio-centos.sh' >> /app/demo/run.sh \
     && echo './meetingSDKDemo' >> /app/demo/run.sh

 # Make the run script executable
 RUN chmod +x /app/demo/run.sh

 # Run the meetingSDKDemo binary when the container starts
 CMD ["/app/demo/run.sh"]
 #CMD ["/bin/bash"]
 #CMD ["./meetingSDKDemo"]
