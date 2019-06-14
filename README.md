# BMK-SDK
Modified version of kittipong-y/bmk-sdk however this avoids the Segger embedded studio as there is a make file.

Bluetooth mechanical keyboard firmware for nRF52 SoC using nRF5 SDK

## Features
(from bmk)
* [x] Basic functionality.
    * [x] Basic keys.
    * [x] Shifted keys.
    * [x] Multi-layer support.
    * [x] Master-to-slave link.
* [x] Devices connectivity. Can connect up to 3 devices and switch between them.
* [x] Low power mode (low power idle state).
* [ ] Media keys.???

(kwakeham)
* [x] S132 - good ole S132
* [ ] S332 - For, reasons
* [ ] clean master files so it's more readable


## Supported board.
* BlueMicro? Probably still, nothing changed enough to avoid this
* nrf52-dk dev kit

## Supported keyboard.
* ErgoTravel? Probably

## Setup
1. Get GCC for embedded working [from this tutorial](https://devzone.nordicsemi.com/nordic/nordic-blog/b/blog/posts/development-with-gcc-and-eclipse). Make sure to get your tool chain setup and edit the <SDK>/components/toolchain/gcc windows/mac/linux files for your os. Version and folders are important.
   
2. Download and extract [nrf5 sdk](https://www.nordicsemi.com/Software-and-Tools/Software/nRF5-SDK) into a proper folder along with this project. It should look like this:
    ```
    .
    +-- nrf5_sdk (This folder contains all contents of nRF5 SDK.)
    |   +-- components
    |   +-- config
    |   +-- documentation
    |   +-- ...
    +-- bmk-sdk (This project folder.)
        +-- src
        +-- ...
    ```
    All referenced source files and headers will be resolved to folder nRF5_SDK above.
    
    This is tested on SDK 15.3.0 with S132 6.1.1 but remember, nordic makes radical changes to the SDK breaking stuff all the time. Be careful with new revisions.
    
3. Open folder in vscode (or editor of your preference).

4. Build and flash your firmware using commandline 'make' in the PCA10040/S132/armgcc folder. 
