# mem_wrapper
A wrapper library that intercept memory related functions, based on function interposition

## How to build
Invoke `make` should generate **mem_wrapper.so**

## How to use
**mem_wrapper.so** is a preload library, one needs to set  *LD_PRELOAD* environment variable to use it.
An example: 
  $> `export LD_PRELOAD=PATH_TO_MEM_WRAPPER.SO`
  $> `./my_application`


