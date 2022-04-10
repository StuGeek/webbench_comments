/*
 * (C) Radim Kolar 1997-2004
 * This is free software, see GNU Public License version 2 for
 * details.
 *
 * Simple forking WWW Server benchmark:
 *
 * Usage:
 *   webbench --help
 *
 * Return codes:
 *    0 - sucess
 *    1 - benchmark failed (server is not on-line)
 *    2 - bad param
 *    3 - internal error, fork failed
 * 
 */ 
#include "socket.c"
#include <unistd.h>
#include <sys/param.h>
#include <rpc/types.h>
#include <getopt.h>
#include <strings.h>
#include <time.h>
#include <signal.h>

/* values */
volatile int timerexpired=0;   // 表示计时器是否到期，到期为1，否则为0
int speed=0;                   // 传送速率（先获得测试成功次数，后面除以时间得到真正的传输速率）
int failed=0;                  // 测试的失败数
int bytes=0;                   // 总共传送的字节数
/* globals */
// 使用的http协议版本，http/0.9为0，http/1.0为1，http/1.1为2
int http10=1; /* 0 - http/0.9, 1 - http/1.0, 2 - http/1.1 */
/* Allow: GET, HEAD, OPTIONS, TRACE */
#define METHOD_GET 0           // 方法GET的宏定义为0
#define METHOD_HEAD 1          // 方法HEAD的宏定义为1
#define METHOD_OPTIONS 2       // 方法OPTIONS的宏定义为2
#define METHOD_TRACE 3         // 方法TRACE的宏定义为3
#define PROGRAM_VERSION "1.5"  // 程序的版本为1.5，即webbench1.5
int method=METHOD_GET;         // 请求方式默认为GEI
int clients=1;                 // 客户端数量默认为2
int force=0;                   // 是否不等待服务器响应，发送请求后直接关闭连接，默认需要等待
int force_reload=0;            // 是否强制代理服务器重新发送请求，默认不发送
int proxyport=80;              // 访问端口默认为80
char *proxyhost=NULL;          // 代理服务器，默认无
int benchtime=30;              // 测试运行时间默认为30s
/* internal */
int mypipe[2];                 // 读写管道，0为读取端，1为写入端
char host[MAXHOSTNAMELEN];     // 目标服务器的网络地址
#define REQUEST_SIZE 2048      // 请求的最大长度
char request[REQUEST_SIZE];    // 请求内容

// 构造长选项与短选项的对应关系，no_argument表示选项没有参数，required_argument表示选项需要参数
static const struct option long_options[]=
{
 {"force",no_argument,&force,1},
 {"reload",no_argument,&force_reload,1},
 {"time",required_argument,NULL,'t'},
 {"help",no_argument,NULL,'?'},
 {"http09",no_argument,NULL,'9'},
 {"http10",no_argument,NULL,'1'},
 {"http11",no_argument,NULL,'2'},
 {"get",no_argument,&method,METHOD_GET},
 {"head",no_argument,&method,METHOD_HEAD},
 {"options",no_argument,&method,METHOD_OPTIONS},
 {"trace",no_argument,&method,METHOD_TRACE},
 {"version",no_argument,NULL,'V'},
 {"proxy",required_argument,NULL,'p'},
 {"clients",required_argument,NULL,'c'},
 {NULL,0,NULL,0}
};

/* prototypes */
static void benchcore(const char* host,const int port, const char *request);
static int bench(void);
static void build_request(const char *url);

// 信号处理函数
static void alarm_handler(int signal)
{
   // 设置计时器到期
   timerexpired=1;
}	

// 用法，描述了参数设置，可打印出来
static void usage(void)
{
   fprintf(stderr,
   // 用法是webbench后面加相应的选项，最后加上要测试的URL
	"webbench [option]... URL\n"
   // 不用等待服务器的响应，发送请求后直接关闭连接
	"  -f|--force               Don't wait for reply from server.\n"
	// 发送重新加载请求（无缓存）
   "  -r|--reload              Send reload request - Pragma: no-cache.\n"
	// 运行基准测试的秒数，默认为30s
   "  -t|--time <sec>          Run benchmark for <sec> seconds. Default 30.\n"
	// 使用代理服务器发送请求
   "  -p|--proxy <server:port> Use proxy server for request.\n"
	// 一次运行的http客户端个数，默认为1
   "  -c|--clients <n>         Run <n> HTTP clients at once. Default one.\n"
	// 使用HTTP/0.9协议
   "  -9|--http09              Use HTTP/0.9 style requests.\n"
	// 使用HTTP/1.0协议
   "  -1|--http10              Use HTTP/1.0 protocol.\n"
	// 使用HTTP/1.1协议
   "  -2|--http11              Use HTTP/1.1 protocol.\n"
	// 请求方法使用GET
   "  --get                    Use GET request method.\n"
	// 请求方法使用HEAD
   "  --head                   Use HEAD request method.\n"
	// 请求方法使用OPTIONS
   "  --options                Use OPTIONS request method.\n"
	// 请求方法使用TRACE
   "  --trace                  Use TRACE request method.\n"
	// 打印帮助信息
   "  -?|-h|--help             This information.\n"
	// 显示程序版本
   "  -V|--version             Display program version.\n"
	);
};
int main(int argc, char *argv[])
{
 int opt=0;
 int options_index=0;
 char *tmp=NULL;
 
 // 如果在终端只输入了./webbench，后面没有跟参数，打印用法，直接退出程序，返回码为2，表示格式错误
 if(argc==1)
 {
	  usage();
          return 2;
 } 

 // 循环解析终端输入选项，每次解析一个选项及其后面可能跟的参数
 while((opt=getopt_long(argc,argv,"912Vfrt:p:c:?h",long_options,&options_index))!=EOF )
 {
  switch(opt)
  {
   case  0 : break;
   // 如果选项为'f'，那么设置不等待服务器响应，发送请求后直接关闭连接
   case 'f': force=1;break;
   // 如果选项为'r'，那么设置强制代理服务器重新发送请求
   case 'r': force_reload=1;break; 
   // 如果选项为'9'，那么设置在条件允许范围内使用HTTP/0.9协议
   case '9': http10=0;break;
   // 如果选项为'1'，那么设置在条件允许范围内使用HTTP/1.0协议
   case '1': http10=1;break;
   // 如果选项为'2'，那么设置在条件允许范围内使用HTTP/1.1协议
   case '2': http10=2;break;
   // 如果选项为'V'，那么打印程序版本，然后退出程序
   case 'V': printf(PROGRAM_VERSION"\n");exit(0);
   // 如果选项为't'，那么记录其后所跟参数数值到运行基准时间benchtime中
   case 't': benchtime=atoi(optarg);break;
   // 如果选项为'p'，那么表示使用代理服务器	     
   case 'p': 
	     /* proxy server parsing server:port */
        // 记录参数中最后出现字符':'的位置及其之后的内容到tmp中
	     tmp=strrchr(optarg,':');
        // 记录参数到代理主机proxyhost中
	     proxyhost=optarg;
        // 如果参数中没有字符':'，说明没有端口号，直接退出switch
	     if(tmp==NULL)
	     {
		     break;
	     }
        // 如果参数中只有一个字符':'，说明端口号在最前，打印缺失主机名，然后直接返回，返回码为2，表示格式错误
	     if(tmp==optarg)
	     {
		     fprintf(stderr,"Error in option --proxy %s: Missing hostname.\n",optarg);
		     return 2;
	     }
        // 如果参数中最后一个':'之后没有内容，打印缺失端口号，然后直接返回，返回码为2，表示格式错误
	     if(tmp==optarg+strlen(optarg)-1)
	     {
		     fprintf(stderr,"Error in option --proxy %s Port number is missing.\n",optarg);
		     return 2;
	     }
        // 将proxyhost中的内容从最后一个':'处进行截断，只记录':'之前的内容
	     *tmp='\0';
        // 将最后一个':'之后内容转化为数字并记录在代理服务器端口号proxyport中
	     proxyport=atoi(tmp+1);break;
   // 如果选项为':'、'h'、'?'，那么打印用法，并直接退出程序，返回码为2，表示格式错误
   case ':':
   case 'h':
   case '?': usage();return 2;break;
   // 如果选项为'c'，那么记录其后所跟参数数值到客户端数量clients中
   case 'c': clients=atoi(optarg);break;
  }
 }
 
 // 如果参数后没有其它内容，打印缺失测试URL，打印用法后直接退出程序，返回码为2，表示格式错误
 if(optind==argc) {
                      fprintf(stderr,"webbench: Missing URL!\n");
		      usage();
		      return 2;
                    }

 // 如果输入客户端数量为0，则设置为默认值1
 if(clients==0) clients=1;
 // 如果输入运行测试的秒数为0，则设置为默认值60
 if(benchtime==0) benchtime=60;
 /* Copyright */
 // 打印版权信息
 fprintf(stderr,"Webbench - Simple Web Benchmark "PROGRAM_VERSION"\n"
	 "Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.\n"
	 );
 // 将测试URL作为参数传入build_request方法中，构建http请求
 build_request(argv[optind]);
 /* print bench info */
 // 打印Benchmarking、请求方法、测试URL
 printf("\nBenchmarking: ");
 switch(method)
 {
	 case METHOD_GET:
	 default:
		 printf("GET");break;
	 case METHOD_OPTIONS:
		 printf("OPTIONS");break;
	 case METHOD_HEAD:
		 printf("HEAD");break;
	 case METHOD_TRACE:
		 printf("TRACE");break;
 }
 printf(" %s",argv[optind]);
 // 判断使用的http协议类型，如果使用的是默认的HTTP/1.0则不打印
 switch(http10)
 {
    // 如果http10的值为0，打印使用HTTP/0.9
	 case 0: printf(" (using HTTP/0.9)");break;
    // 如果http10的值为2，打印使用HTTP/1.1
	 case 2: printf(" (using HTTP/1.1)");break;
 }
 printf("\n");
 // 打印客户端数、运行秒数
 if(clients==1) printf("1 client");
 else
   printf("%d clients",clients);

 printf(", running %d sec", benchtime);
 // 打印是否不等待响应就提前关闭连接、是否通过代理服务器发送请求，是否无缓存
 if(force) printf(", early socket close");
 if(proxyhost!=NULL) printf(", via proxy server %s:%d",proxyhost,proxyport);
 if(force_reload) printf(", forcing reload");
 printf(".\n");
 // 开始压力测试
 return bench();
}

// 利用传入的测试url参数，构建对测试url的http请求
void build_request(const char *url)
{
  char tmp[10];
  int i;

  bzero(host,MAXHOSTNAMELEN);
  bzero(request,REQUEST_SIZE);
  
  // 缓存和代理需要HTTP/1.0及以上才能使用，自动调整使用http协议版本
  if(force_reload && proxyhost!=NULL && http10<1) http10=1;
  // 请求方法HEAD需要HTTP/1.0及以上才能使用，自动调整使用http协议版本
  if(method==METHOD_HEAD && http10<1) http10=1;
  // 请求方法OPTIONS和TRACE需要HTTP/1.1及以上才能使用，自动调整使用http协议版本
  if(method==METHOD_OPTIONS && http10<2) http10=2;
  if(method==METHOD_TRACE && http10<2) http10=2;

  // 根据请求方法填充请求报文的请求行
  switch(method)
  {
	  default:
	  case METHOD_GET: strcpy(request,"GET");break;
	  case METHOD_HEAD: strcpy(request,"HEAD");break;
	  case METHOD_OPTIONS: strcpy(request,"OPTIONS");break;
	  case METHOD_TRACE: strcpy(request,"TRACE");break;
  }
		  
  strcat(request," ");

  /* 判断URL的格式合法性 */

  // 如果url中没有"://"，打印是无效的URL，直接退出程序，返回码为2，表示格式错误
  if(NULL==strstr(url,"://"))
  {
	  fprintf(stderr, "\n%s: is not a valid URL.\n",url);
	  exit(2);
  }
  // 如果url的长度大于1500，打印URL过长，直接退出程序，返回码为2，表示格式错误
  if(strlen(url)>1500)
  {
         fprintf(stderr,"URL is too long.\n");
	 exit(2);
  }
  // 如果不使用代理服务器
  if(proxyhost==NULL)
      // 如果url的前7位不是任意大小写的"http://"，打印仅直接支持HTTP协议，可能需要选择使用代理服务器的选项，直接退出程序，返回码为2，表示格式错误
	   if (0!=strncasecmp("http://",url,7)) 
	   { fprintf(stderr,"\nOnly HTTP protocol is directly supported, set --proxy for others.\n");
             exit(2);
           }
  /* protocol/host delimiter */
  // 定位目标主机名的开始位置
  i=strstr(url,"://")-url+3;
  /* printf("%d\n",i); */

  // 如果主机名后没有'/'，说明主机名没有以'/'结尾，打印是无效的URL，直接退出程序
  if(strchr(url+i,'/')==NULL) {
                                fprintf(stderr,"\nInvalid URL syntax - hostname don't ends with '/'.\n");
                                exit(2);
                              }
   /* 判断完url的合法性后填写url到请求行 */
         
  // 没有设置代理服务器时
  if(proxyhost==NULL)
  {
   /* get port from hostname */
   // 从主机名之后开始寻找':'所在的位置，如果':'存在且位置在'/'之前
   if(index(url+i,':')!=NULL &&
      index(url+i,':')<index(url+i,'/'))
   {
      // 将找到的':'之前的内容，即去掉端口号，复制进目标服务器地址host中
	   strncpy(host,url+i,strchr(url+i,':')-url-i);
	   bzero(tmp,10);
      // 将':'之后的内容，即端口号，复制进tmp中，并转换为数字存进proxyport中
	   strncpy(tmp,index(url+i,':')+1,strchr(url+i,'/')-index(url+i,':')-1);
	   /* printf("tmp=%s\n",tmp); */
	   proxyport=atoi(tmp);
      // 如果proxyport值为0，有可能写了':'但没写端口号，那么设置为默认端口号80
	   if(proxyport==0) proxyport=80;
   } else  // 如果没找到':'，说明没有端口号
   {
     // 将主机名之后，"/"之前的内容复制进host中
     strncpy(host,url+i,strcspn(url+i,"/"));
   }
   // printf("Host=%s\n",host);
   // 将主机名之后找到的"/"之后的内容拼接到请求行的相应位置
   strcat(request+strlen(request),url+i+strcspn(url+i,"/"));
  } else
  {
   // printf("ProxyHost=%s\nProxyPort=%d\n",proxyhost,proxyport);
   // 设置了代理服务器时就直接将url拼接进请求行中
   strcat(request,url);
  }
  // 将http协议版本及其之后的"\r\n"拼接到请求行中
  if(http10==1)
	  strcat(request," HTTP/1.0");
  else if (http10==2)
	  strcat(request," HTTP/1.1");
  strcat(request,"\r\n");

  /* 填写请求头 */
  
  // 如果使用的是HTTP/1.0及其之后的版本，拼接用户代理到请求头中
  if(http10>0)
	  strcat(request,"User-Agent: WebBench "PROGRAM_VERSION"\r\n");
  // 如果没有使用代理服务器且使用的是HTTP/1.0及其之后的版本，拼接目标服务器地址到请求头中
  if(proxyhost==NULL && http10>0)
  {
	  strcat(request,"Host: ");
	  strcat(request,host);
	  strcat(request,"\r\n");
  }
  // 如果设置了强制代理服务器重新发送请求且代理服务器不为空，拼接不使用缓存到请求头中
  if(force_reload && proxyhost!=NULL)
  {
	  strcat(request,"Pragma: no-cache\r\n");
  }
  // 如果使用的是HTTP/1.1协议，因为不用传输任何内容，那么拼接不使用长连接到请求头中，这样可以降低维护连接的消耗
  if(http10>1)
	  strcat(request,"Connection: close\r\n");
  /* add empty line at end */
  // 最后填入空行完成构建请求头
  if(http10>0) strcat(request,"\r\n"); 
  // printf("Req=%s\n",request);
}

/* vraci system rc error kod */
// 压力测试方法，创建指定个数子进程客户端，不断对目标服务器或代理服务器发起连接请求，并统计相应数据
static int bench(void)
{
  int i,j,k;	
  pid_t pid=0;
  FILE *f;

  /* check avaibility of target server */
  // 建立一个TCP连接，检查连接可用性，如果设置了代理服务器，那么连接代理服务器，否则直接连接目标服务器
  i=Socket(proxyhost==NULL?host:proxyhost,proxyport);
  // 连接失败那么打印错误信息，并直接退出，返回码为1，表示基准测试失败（服务器未联机）
  if(i<0) { 
	   fprintf(stderr,"\nConnect to server failed. Aborting benchmark.\n");
           return 1;
         }
  // 关闭连接，这次连接不计入测试
  close(i);
  /* create pipe */
  // 创建管道，如果失败，直接退出程序，返回码为3，表示内部错误
  if(pipe(mypipe))
  {
	  perror("pipe failed.");
	  return 3;
  }

  /* not needed, since we have alarm() in childrens */
  /* wait 4 next system clock tick */
  /*
  cas=time(NULL);
  while(time(NULL)==cas)
        sched_yield();
  */

  /* fork childs */
  // 创建clients个子进程，由子进程进行真正的测试
  for(i=0;i<clients;i++)
  {
	   pid=fork();
      // 如果是子进程或者创建失败，休眠1s后退出循环，让父进程先执行，完成初始化，并且保证子进程中不会再fork出新的子进程
	   if(pid <= (pid_t) 0)
	   {
		   /* child process or error*/
	           sleep(1); /* make childs faster */
		   break;
	   }
  }

  // 如果创建子进程失败，那么打印fork失败，直接退出程序，返回码为3，表示fork失败
  if( pid< (pid_t) 0)
  {
          fprintf(stderr,"problems forking worker no. %d\n",i);
	  perror("fork failed.");
	  return 3;
  }

  // 在子进程中
  if(pid== (pid_t) 0)
  {
    /* I am a child */
    // 如果不使用代理服务器，那么子进程直接对目标服务器发出http请求，否则向代理服务器发出http请求
    if(proxyhost==NULL)
      benchcore(host,proxyport,request);
         else
      benchcore(proxyhost,proxyport,request);

         /* write results to pipe */
    // 获取管道写端的文件指针
	 f=fdopen(mypipe[1],"w");
    // 获取失败，直接退出，返回码为3，表示内部错误
	 if(f==NULL)
	 {
		 perror("open pipe for writing failed.");
		 return 3;
	 }
	 /* fprintf(stderr,"Child - %d %d\n",speed,failed); */
    // 将子进程的传输速率，测试失败数，总传输字节数写入管道，关闭写端
	 fprintf(f,"%d %d %d\n",speed,failed,bytes);
	 fclose(f);
    // 返回0，表示运行成功
	 return 0;
  } else // 父进程中
  {
     // 获取管道读端的指针
	  f=fdopen(mypipe[0],"r");
     // 获取失败，直接退出，返回码为3，表示内部错误
	  if(f==NULL) 
	  {
		  perror("open pipe for reading failed.");
		  return 3;
	  }
     // 设置不使用缓冲。每个I/O操作都被即时写入管道
	  setvbuf(f,NULL,_IONBF,0);
     // 初始化传输速率，测试失败次数，传输总字节数都为0
	  speed=0;
          failed=0;
          bytes=0;

     // 父进程循环读取数据 
	  while(1)
	  {
        // 循环从管道中每3个一组读取子进程的输出数据，并且获取成功读取的参数个数
		  pid=fscanf(f,"%d %d %d",&i,&j,&k);
        // 如果成功读取的个数小于2，说明有子进程中途挂掉，直接退出读取循环
		  if(pid<2)
                  {
                       fprintf(stderr,"Some of our childrens died.\n");
                       break;
                  }
        // 否则更新传输速率（测试成功个数），测试失败次数，传输总字节数
		  speed+=i;
		  failed+=j;
		  bytes+=k;
		  /* fprintf(stderr,"*Knock* %d %d read=%d\n",speed,failed,pid); */
        // 客户端数减一后如果等于0，说明没有多的客户端数据读取，直接退出循环
		  if(--clients==0) break;
	  }
     // 关闭读端
	  fclose(f);
  
  // 打印统计的总的测试传输速度，请求成功数与失败数
  printf("\nSpeed=%d pages/min, %d bytes/sec.\nRequests: %d susceed, %d failed.\n",
		  (int)((speed+failed)/(benchtime/60.0f)),
		  (int)(bytes/(float)benchtime),
		  speed,
		  failed);
  }
  return i;
}

// 子进程在测试时间内不断向服务器建立连接并发送http请求，计时器到期后退出子线程，统计相应数据
void benchcore(const char *host,const int port,const char *req)
{
 int rlen;
 char buf[1500];
 int s,i;
 struct sigaction sa;

 /* setup alarm signal handler */
 // 设置SIGALRM的信号处理函数
 sa.sa_handler=alarm_handler;
 sa.sa_flags=0;
 if(sigaction(SIGALRM,&sa,NULL))
    exit(3);
 // 设置计时器时间为运行测试的时间，到期后发送SIGALRM信号
 alarm(benchtime);
 
 // 获取请求报文大小
 rlen=strlen(req);
 // 进入循环，每次客户端建立一个连接，计时器时间到期后再退出
 nexttry:while(1)
 {
    // 如果timerexpired等于1，说明收到了SIGALRM信号，表示计时器到期了，直接返回
    if(timerexpired)
    {
       // 如果失败的测试数大于0，那么失败的测试数减一
       if(failed>0)
       {
          /* fprintf(stderr,"Correcting failed by signal\n"); */
          failed--;
       }
       return;
    }
    // 建立与目标服务器的TCP连接
    s=Socket(host,port);
    // 如果连接失败，测试的失败数加一，继续循环                          
    if(s<0) { failed++;continue;} 
    // 如果请求报文写入套接字失败，测试的失败数加一，继续循环
    if(rlen!=write(s,req,rlen)) {failed++;close(s);continue;}
    // 如果使用HTTP/0.9协议，因为会在服务器回复后自动断开连接，所以可以先关闭写端
    if(http10==0) 
       // 如果写端关闭失败，那么说明是不正常的连接状态，测试的失败数加一，关闭连接，继续循环
	    if(shutdown(s,1)) { failed++;close(s);continue;}
    // 如果设置需要等待服务器响应，那么还要处理响应数据，否则直接关闭连接
    if(force==0) 
    {
            /* read all available data from socket */
       // 从套接字中读取数据
	    while(1)
	    {
              // 如果计时器到期，结束读取
              if(timerexpired) break; 
         // 将数据读取进buf中
	      i=read(s,buf,1500);
              /* fprintf(stderr,"%d\n",i); */
         // 如果读取失败，测试的失败数加一，关闭连接，继续循环，客户端重新建立连接，直到计时器到期后再退出
	      if(i<0) 
              { 
                 failed++;
                 close(s);
                 goto nexttry;
              }
	       else
             // 如果已经读取到文件末尾，结束读取数据
		       if(i==0) break;
             // 如果读取到了数据，将总共传送的字节数加上读取到的数据的字节数
		       else
			       bytes+=i;
	    }
    }
    // 关闭连接，如果失败，测试失败数加一，继续循环
    if(close(s)) {failed++;continue;}
    // 传输速率（这里用测试成功数表示，后面除以时间得到真正的传输速率）加一
    speed++;
 }
}
