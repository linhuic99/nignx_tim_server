# nignx_tim_server
# 功能描述
  1.让nginx上的服务具备相互感知到对方的存在的能力、多用于服务器检测、寻址、监控  
  2.通讯协议：HTTP
  
# 编译方式
  1.nginx版本：1.10.2
  2.把ngx_http_tim_module放到nginx源码目录中
  3.编译安装nginx(命令如下)
  ./configure --prefix=/home/linhui/nginx --add-module=ngx_http_tim_module
  make
  make install
