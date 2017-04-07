# nignx_tim_server
# 功能描述
  1.让nginx上的服务具备相互感知到对方的存在的能力、多用于服务器检测、寻址、监控  <br/>
  
  2.通讯协议：HTTP
  
# 编译方式
  1.nginx版本：1.10.2<br/>
  2.把ngx_http_tim_module放到nginx源码目录中<br/>
  3.编译安装nginx(命令如下)<br/>
  
  ./configure --prefix=/home/linhui/nginx --add-module=ngx_http_tim_module <br/>
  make <br/>
  make install <br/>

# 使用方式
	
	1.比如在Nginx模块的配置文件 nginx/conf/nginx.conf server段中增加如下配置
	location /tim {
    		tim on;
	}
	2.使用访问地址进行测试
	http://{ip}:{port}/tim/address?