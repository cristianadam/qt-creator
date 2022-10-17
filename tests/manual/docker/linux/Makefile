all: help

help:
	@grep -E '^[0-9a-zA-Z._-]+:.*?## .*$$' $(MAKEFILE_LIST) | awk 'BEGIN {FS = ":.*?## "}; {printf "\033[36m%-30s\033[0m %s\n", $$1, $$2}'

qt-linux: Dockerfile ## Build Ubuntu 22.04, Qt 6, docker-cli image
	docker build . -t qt-linux -t qt-linux:6

qt-linux-run: qt-linux ## Build & run the image
	docker run -it --rm -v /var/run/docker.sock:/var/run/docker.sock -v $(HOME):$(HOME) --workdir=$(HOME) qt-linux:6 /bin/bash

qt-linux-with-qtc: DockerFile-with-creator ## Build Ubuntu 22.04, Qt 6, docker-cli image, Includes Qt Creator
	docker build . -f DockerFile-with-creator -t qt-linux-qtc -t qt-linux-qtc:6

qt-linux-with-qtc-run: qt-linux-with-qtc ## Build & run the image, Includes Qt Creator
	docker run -it --rm  -v $(PWD)/../../../../:/work --env="DISPLAY=host.docker.internal:0" qt-linux-qtc:6 /bin/bash	
