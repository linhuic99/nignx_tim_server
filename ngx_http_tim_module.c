/*
 * ngx_http_tim_module.c
 *
 *      Author: linhui
 */



#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

static char *ngx_http_set_server_handler(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_set_client_handler(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static void *ngx_http_tim_create_loc_conf(ngx_conf_t *cf);

static ngx_int_t ngx_http_tim_init_master(ngx_log_t *log);
static ngx_int_t ngx_http_tim_init_module(ngx_cycle_t *cycle);
static ngx_int_t ngx_http_tim_init_process(ngx_cycle_t *cycle);

static ngx_int_t ngx_http_tim_server_respone(ngx_http_request_t* r);

typedef struct{
	int count;//连接数
	int tmp;//时间戳
	int port;//端口号
	int drpc;//dcpc端口
	char ip[16];//ip地址
}tim_t;

typedef struct {
	uint mask;
	uint top;
	uint idx;
	uint first;//第一个可用
	tim_t row[0];
}tims_t;

static tims_t tims0 = {0};
tims_t *tims = &tims0;
static int work_process = 8;
#define TIM_MASK 0x7FF

static ngx_int_t ngx_tim_push(ngx_http_request_t *r,const tim_t *tim,int times);
static const tim_t* ngx_tim_pop(ngx_http_request_t *r);
static const tim_t* ngx_tim_pop_ex(ngx_http_request_t *r);

typedef struct {
	time_t tim_expiry;
}ngx_http_tim_loc_conf_t;

static ngx_command_t  ngx_http_tim_commands[] = {

	{ ngx_string("tim_server"),
	  NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
	  ngx_http_set_server_handler,
	  0,
	  0,
	  NULL },
	{ ngx_string("tim_expiry"),
	  NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
	  ngx_conf_set_sec_slot,
	  NGX_HTTP_LOC_CONF_OFFSET,
	  offsetof(ngx_http_tim_loc_conf_t, tim_expiry),
	  NULL },
	{ ngx_string("tim_client"),
	  NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
	  ngx_http_set_client_handler,
	  0,
	  0,
	  NULL },

      ngx_null_command
};



static ngx_http_module_t  ngx_http_tim_module_ctx = {
    NULL,             /* preconfiguration */
    NULL,             /* postconfiguration */

    NULL,             /* create main configuration */
    NULL,             /* init main configuration */

    NULL,             /* create server configuration */
    NULL,             /* merge server configuration */

    ngx_http_tim_create_loc_conf,             /* create location configuration */
    NULL              /* merge location configuration */
};


ngx_module_t  ngx_http_tim_module = {
    NGX_MODULE_V1,
    &ngx_http_tim_module_ctx,       /* module context */
    ngx_http_tim_commands,          /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    ngx_http_tim_init_master,       /* init master */
    ngx_http_tim_init_module,       /* init module */
    ngx_http_tim_init_process,      /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};



/*
 * 响应返回处理函数
 */
static ngx_int_t
ngx_http_tim_respond_handler(ngx_http_request_t* r, ngx_str_t s){
	ngx_int_t          rc;
    ngx_buf_t          *buf;
    ngx_chain_t        *out , *p;
    ngx_str_t          iconvbuf;
	size_t sl = 0 , chain_size = 20480;

	iconvbuf = s;


	r->headers_out.content_length_n = iconvbuf.len;
	r->headers_out.status = NGX_HTTP_OK;

	out = p = ngx_alloc_chain_link(r->pool);
	p->buf = NULL;
	p->next = NULL;

	while (iconvbuf.len > sl) {
		p->buf = buf = ngx_calloc_buf(r->pool);
		buf->start = iconvbuf.data;
		buf->end = iconvbuf.data + iconvbuf.len;
		buf->pos = iconvbuf.data + sl;
		buf->temporary = 1;/*在内存块中，可以被filter变更*/
		buf->last_in_chain = 1;/*在当前的chain里面，此buf是最后一个*/
		if (iconvbuf.len - sl <= chain_size) {
			buf->last_buf = 1; /*表明是最后一个buffer*/
			sl = iconvbuf.len;
			buf->last = iconvbuf.data + sl;
		} else {
			buf->last_buf = 0;
			sl += chain_size;
			buf->last = iconvbuf.data + sl;
			{
				p->next = ngx_alloc_chain_link(r->pool);
				p = p->next;
				p->buf = NULL;
				p->next = NULL;
			}
		}
	}

	if (iconvbuf.len > chain_size) {
		r->keepalive = 0;
	}

	rc = ngx_http_send_header(r);
	rc = ngx_http_output_filter(r, out);
	//ngx_http_finalize_request(r, rc);

	return rc;
}

static ngx_int_t ngx_http_tim_server_respone(ngx_http_request_t* r){
	ngx_int_t          i,j;
	ngx_int_t          num = 0;
	tim_t             *Ltims,*Mtims;
	ngx_str_t   rs;
	u_char *p;


	Mtims = tims->row;
	Ltims = (tim_t*)ngx_palloc(r->pool,sizeof(tim_t)*(TIM_MASK));

	for(i = tims->idx;i<tims->top;i++){
		if(Mtims[i&tims->mask].tmp < ngx_time())
			continue;
		for(j=0;j<num;j++){
			if(Mtims[i&tims->mask].port == Ltims[j].port && ngx_strcmp(Mtims[i&tims->mask].ip,Ltims[j].ip) == 0){
				break;
			}
		}
		if(j == num){
			ngx_memcpy(&Ltims[num++],&Mtims[i&tims->mask],sizeof(tim_t));
		}
	}

	p = rs.data = ngx_palloc(r->pool,64*num + 2);
	*p++ = '[';
	for(i=0;i<num;i++){
		p = ngx_sprintf(p,"{\"ip\":\"%s\",\"port\":%d,\"drpc\":%d},",Ltims[i].ip,Ltims[i].port,Ltims[i].drpc);
	}
	if(num != 0)
		p--;
	*p++ = ']';
	rs.len = p - rs.data;
	return ngx_http_tim_respond_handler(r,rs);
}

static ngx_int_t
ngx_http_tim_server_handler(ngx_http_request_t *r)
{
	tim_t tim = {0};
	ngx_str_t ip = ngx_null_string;
	ngx_str_t port = ngx_null_string;
	ngx_str_t drpc = ngx_null_string;
	ngx_str_t count = ngx_null_string;
	//ngx_str_t rs = ngx_string("注册成功");
	ngx_str_t rr = ngx_string("register failed");
	ngx_http_tim_loc_conf_t *conf = ngx_http_get_module_loc_conf(r, ngx_http_tim_module);
	//ngx_core_conf_t *ccf = (ngx_core_conf_t *) ngx_get_conf(r->connection->, ngx_core_module);

    if (r->method != NGX_HTTP_GET && r->method != NGX_HTTP_HEAD) {
        return NGX_HTTP_NOT_ALLOWED;
    }
    //tim-server    tim/server/register?ip=&port=&drpc=&count=
	u_char *x = ngx_strnstr(r->args.data, "ip=", r->args.len);
	if (x) {
		x += sizeof("ip=") - 1;
		ip.data = x;
		while ((size_t) (x - r->args.data) < r->args.len && *x != (u_char) '&') {
			x++;
		}
		ip.len = (size_t) ((x - ip.data) > 16 ? 16 : (x - ip.data));
	}
	x = ngx_strnstr(r->args.data, "port=", r->args.len);
	if (x) {
		x += sizeof("port=") - 1;
		port.data = x;
		while ((size_t) (x - r->args.data) < r->args.len && *x != (u_char) '&') {
			x++;
		}
		port.len = (size_t) ((x - port.data) > 16 ? 16 : (x - port.data));
	}
	x = ngx_strnstr(r->args.data, "drpc=", r->args.len);
	if (x) {
		x += sizeof("drpc=") - 1;
		drpc.data = x;
		while ((size_t) (x - r->args.data) < r->args.len && *x != (u_char) '&') {
			x++;
		}
		drpc.len = (size_t) ((x - drpc.data) > 16 ? 16 : (x - drpc.data));
	}
	x = ngx_strnstr(r->args.data, "count=", r->args.len);
	if (x) {
		x += sizeof("count=") - 1;
		count.data = x;
		while ((size_t) (x - r->args.data) < r->args.len && *x != (u_char) '&') {
			x++;
		}
		count.len = (size_t) ((x - count.data) > 16 ? 16 : (x - count.data));
	}

	if(port.len == 0 || ip.len == 0){
		return ngx_http_tim_respond_handler(r,rr);
	}

	strncpy(tim.ip,(const char*)ip.data,ip.len);
	tim.port = atol((const char*)port.data);
	tim.drpc = atol((const char*)drpc.data);
	if(conf->tim_expiry == NGX_CONF_UNSET){
		tim.tmp = ngx_time() + 5;//默认5秒后失效
	}else{
		tim.tmp = ngx_time() + conf->tim_expiry;
	}
	if(count.data != NULL){
		tim.count = atol((const char*)count.data);
	}
	ngx_tim_push(r,&tim,1);

	//return ngx_http_tim_respond_handler(r,rs);
	return ngx_http_tim_server_respone(r);
}

static ngx_int_t
ngx_http_tim_client_handler(ngx_http_request_t *r)
{
	ngx_str_t rs;
	ngx_str_t rr = ngx_string("{\"ip\":\"\", \"port\":0}");
	const tim_t *rt;
	u_char *p;

    if (r->method != NGX_HTTP_GET && r->method != NGX_HTTP_HEAD) {
        return NGX_HTTP_NOT_ALLOWED;
    }
    //tim-client    tim/client/address?account=
	rt = ngx_tim_pop_ex(r);
	if(rt == NULL){
		return ngx_http_tim_respond_handler(r,rr);
	}

	rs.data = ngx_palloc(r->pool,1024);
	p = ngx_sprintf(rs.data,"{\"ip\":\"%s\",\"port\":%d}",rt->ip,rt->port);
	rs.len = p - rs.data;
	return ngx_http_tim_respond_handler(r,rs);
}


static char *
ngx_http_set_server_handler(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_tim_server_handler;
    ngx_log_error(NGX_LOG_NOTICE, cf->log, 0,
    						"ngx_http_set_server_handler");
    return NGX_CONF_OK;
}

static char *
ngx_http_set_client_handler(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_tim_client_handler;
    ngx_log_error(NGX_LOG_NOTICE, cf->log, 0,
    						"ngx_http_set_client_handler");
    return NGX_CONF_OK;
}

static void *ngx_http_tim_create_loc_conf(ngx_conf_t *cf) {
	ngx_http_tim_loc_conf_t *conf;
	conf = (ngx_http_tim_loc_conf_t*) ngx_pcalloc(cf->pool,
			sizeof(ngx_http_tim_loc_conf_t));

	if (conf == NULL) {
		return NULL;
	}

	conf->tim_expiry = NGX_CONF_UNSET;

	return conf;
}

static ngx_int_t ngx_http_tim_init_master(ngx_log_t *log){
	ngx_log_error(NGX_LOG_NOTICE, log, 0,
						"ngx_http_tim_init_master");
	return NGX_OK;
}

static ngx_int_t ngx_http_tim_init_module(ngx_cycle_t *cycle){
    u_char              *shared;
    ngx_shm_t            shm;
	ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0,
						"ngx_http_tim_init_module");
	/**
	 * 创建匿名共享内存
	 */
    shm.size = sizeof(tims_t)+sizeof(tim_t)*(TIM_MASK);
    shm.name.len = sizeof("nginx_shm_demo");
    shm.name.data = (u_char *) "nginx_shm_demo";
    shm.log = cycle->log;
    if (ngx_shm_alloc(&shm) != NGX_OK) {
        return NGX_ERROR;
    }
    shared = shm.addr;
    tims = (tims_t*)shared;
    tims->mask = TIM_MASK;
    tims->idx = tims->top = 0;

	return NGX_OK;
}

static ngx_int_t ngx_http_tim_init_process(ngx_cycle_t *cycle){
	ngx_core_conf_t *ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);
	ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0,
						"ngx_http_tim_init_process");
	work_process = ccf->worker_processes;
	return NGX_OK;
}

static ngx_int_t ngx_tim_push(ngx_http_request_t *r,const tim_t *tim,int times){
	ngx_uint_t top = ngx_atomic_fetch_add(&tims->top,times);
	int i;
/*	if(tims->top < tims->idx + 1){
		ngx_tim_push(r,tim,1);
	}*/

	for(i=0;i<times;i++){
		ngx_memcpy(&tims->row[(top+i)&tims->mask],tim,sizeof(tim_t));
	}

	return 0;
}

static const tim_t* ngx_tim_pop(ngx_http_request_t *r){
	ngx_uint_t curr;
	if(tims->first >= tims->top){
		/**
		 * 已经没有可用地址
		 */
		return NULL;
	}
	curr = ngx_atomic_fetch_add(&tims->idx,1);
	if(curr >= tims->top){
		//ngx_atomic_fetch_add(&tims->idx,-1);
		/**
		 * 如果队列找完了，重新找，预防注册地址小于work进程数时，pop后未及时push的问题
		 */
		ngx_atomic_cmp_set(&tims->idx,tims->idx,tims->first);
		return ngx_tim_pop(r);
	}
	curr &= tims->mask;
	return &tims->row[curr];
}

static const tim_t* ngx_tim_pop_ex(ngx_http_request_t *r){
	tim_t *rt = NULL;
	const tim_t *p = (tim_t *)-1;
	while(p == (tim_t *)-1){
		p = ngx_tim_pop(r);
		if(p != NULL){
			if(p->tmp > ngx_time()){
				rt = (tim_t*)ngx_palloc(r->pool,sizeof(tim_t));
				ngx_memcpy(rt,p,sizeof(tim_t));
				ngx_tim_push(r,rt,1);
			}else{
				/**
				 * 这里调用的set不严谨
				 */
				ngx_atomic_cmp_set(&tims->first,tims->first,tims->idx);
				p = (tim_t *)-1;
			}
		}
	}
	return rt;
}

