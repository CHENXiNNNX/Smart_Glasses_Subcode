## DeskBot demo

### 1.编译
1. **在电脑上运行SDL仿真**

    注意更改conf/dev_conf, 将`LV_USE_SIMULATOR`置`1`
    
    
    ```sh
    cd ./DeskBot_demo
    mkdir ./build
    cd ./build
    cmake ..
    make
    ```

    然后就可以在ubuntu上运行了

    ```sh
    cd ../bin
    ./main
    ```

2. **编译到开发板上运行**

    注意更改conf/dev_conf, 将`LV_USE_SIMULATOR`置`0`

    ```sh
    cd ./build
    cmake .. -DTARGET_ARM=ON
    make
    ```

    然后把可执行文件所在的文件夹`/bin`, copy到开发板, 在开发板上就能运行了

    **注意:** 

    ```sh
    cd ../bin
    ./main
    ```

### 2.Server运行

python环境搭建详见[AI语言助手demo(Server端)](../AIChat_demo/Server/README.md), 然后进入你搭好的虚拟环境中，运行即可

``` sh
python ./main.py --access_token="123456"
```

### 3.注意

**!! 使用前请先修改`./bin/system_para.conf`中的内容 !!**

1. `./bin/system_para.conf`中有高德API key，需要换成你的API key，用于天气等数据的获取

2. 每个page可视为一个APP, 想要自行添加可以参考模版添加

3. 你的个人电脑运行服务端`server`时, 需要跟开发板`Client`端, 连接到同一个网络下, AI chat才能正常访问. 当然, 你也可以使用电脑进行网络共享(默认开发板是172.32.0.93, 电脑是172.32.0.100).