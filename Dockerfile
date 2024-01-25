FROM ubuntu:22.04

RUN apt -y update && apt -y install sudo

COPY BuildLinux.sh .

RUN sudo ./BuildLinux.sh -u

RUN apt-get clean autoclean && \
		apt-get autoremove --yes && \
		rm -rf /var/lib/{apt,dpkg,cache,log}/

RUN useradd -ms /bin/bash orcaslicer

USER orcaslicer
WORKDIR /home/orcaslicer

RUN git config --global --add safe.directory /home/orcaslicer

ENTRYPOINT ["sh", "./docker-entrypoint.sh"]
