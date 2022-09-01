# Boot2Qt support containers ( for Raspberry PI 4 )

> **Important** Downloading the toolchain only works from within the company VPN.

> **Warning**: You need to give the docker container at least 16GB of RAM, otherwise compilation will fail!

> **Important**: If the build process fails, it might make more sense to start fresh by first removing the volume and deploy folder: `docker volume rm baking && rm -rf deploy` as the restart takes way longer or might hang indefinitely.

> **Warning** For an image build, you need ~ 200GB diskspace, but for a toolchain build 500GB+ is needed.

## Building the Boot2Qt Image
### Dockerfile-qt-6-b2qt-ubuntu-20.04-build

This Dockerfile is used to build Qt 6 for Boot2Qt. It is based on the official Ubuntu 20.04 image and installs the required dependencies.
It also downloads the necessary repo and initializes it for building a Raspeberry Pi 4 image.

### Preparing the build Dockerfile

Once you have build the docker image:

`docker build . -f Dockerfile-qt-6-b2qt-ubuntu-20.04-RPI4-build -t boot2qt-rpi4-builder --build-arg USER_ID=$UID`

You can then create the container:

```
docker run -it --rm \
    -v baking:/b2qt/build-raspberrypi4-64/tmp:rw \
    -v baking-state:/b2qt/state \
    -v $PWD/deploy:/b2qt/build-raspberrypi4-64/tmp/deploy \
    --user $UID \
    boot2qt-rpi4-builder
```

This will create a docker volume called `baking` for all the intermediate build files.
The volume `baking-state` is used for downloads and cache information.

A local folder `./deploy` is mounted which will contain the final image and other artifacts.

### Starting the actual bitbake process

The image has a prepared .bash_history, so after starting just press the up arrow to get the command to build the image:

`$> bitbake b2qt-embedded-qt6-image`

### Writing the Image to SD card

#### macOS

`$> sudo bmaptool copy b2qt-embedded-qt6-image-raspberrypi4-64.wic.xz --bmap b2qt-embedded-qt6-image-raspberrypi4-64.wic.bmap  /dev/rdisk<X>`

## Building the Boot2Qt Toolchain Image

### Building the Toolchain

The image has another command prepared in the .bash_history to prepare the toolchain files:

`$> bitbake meta-toolchain-b2qt-embedded-qt6-sdk`

This creates a .sh installer file in the deploy folder. 

### Preparing the toolchain docker image

To create the actual toolchain docker image you have to build it with the following command:

`$> docker build . -f Dockerfile-qt-6-b2qt-ubuntu-20.04-RPI4-toolchain -t boot2qt-rpi4-toolchain --build-arg toolchain_source=deploy/sdk/toolchain-file-name.sh`

Make sure to replace the toolchain_source argument to match your specific toolchain file name.

