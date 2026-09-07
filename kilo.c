#define _GNU_SOURCE
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#include<termios.h>
#include<unistd.h>//unix header
#include<stdlib.h>
#include<ctype.h>
#include<stdio.h>
#include<errno.h>
#include<string.h>
#include<sys/ioctl.h>
#include<sys/types.h>
#include<stdarg.h>
#include<time.h>
#include<fcntl.h>//File Control
/* define*/
#define CTRL_KEY(k) ((k)& 0x1f) //get low 5bit ,changed "ctrl + k"
#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8
/*function declare*/
void editorMoveCursor(int  key); 
void editorRefreshScreen();
typedef struct erow
{
    int size;
    int rsize;//渲染的字符指针size
    char *chars;
    char *render;//实际渲染的字符指针 包含tab
}erow;
/*
Backspace 键  发送 127 (DEL)
Delete 键 发送转义序列 ESC [ 3 
Ctrl+H 发送 8 (BS)
*/
enum editorKey{
    BACKSPACE = 127,    
    ARROW_LEFT=1000,
    ARROW_RIGHT=1001,
    ARROW_UP=1002,
    ARROW_DOWN=1003,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN,
};

struct editConfig
{
    struct termios orig_termios;
    int screenrows;
    int screencols;
    int numrows;
    int rowoff;//using load all file content
    int coloff;
    int rx;//Render X 实际上渲染的列
    erow* row;//text
    char* filename;
    char statusmsg[80];
    time_t statusmsg_time;
    int cx,cy;//cursor position cx光标在字符数组中的逻辑索引/下标
};
struct editConfig E;

struct abuf
{
   char* b;
   int len;
};
#define ABUF_INIT {NULL,0} //as a constructor for our abuf type.
void abAppend(struct abuf* ab,const char* s,int len){
    char* new=realloc(ab->b,ab->len+len);
    if(new==NULL)return;
    memcpy(&new[ab->len],s,len);
    ab->b=new;
    ab->len+=len;
}
void abFree(struct abuf* ab){
    free(ab->b);
}
void die(const char *s){
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}

/// @brief 计算光标在“逻辑字符数组”中的位置 对应到“屏幕渲染”上的实际列位置rx
/// @param row 储存行信息的结构体
/// @param cx “屏幕渲染”上的实际列位置
/// @return 实际列位置rx
int editorRowCxToRx(erow *row ,int cx){
    int rx=0;
    int i;
    for ( i = 0; i < cx; i++)
    {
        if (row->chars[i]=='\t')
            rx+=(KILO_TAB_STOP-1)-(rx%KILO_TAB_STOP);
        rx++;
    }
    return rx;
}
/// @brief 处理tab用空格代替tab 将row chars中数据赋值在render
/// @param row 文本信息
void editorUpdateRow(erow* row){
    int tabs=0;
    int i;
    for ( i = 0; i < row->size; i++)
    {
        if(row->chars[i]=='\t') tabs++;
    }
    free(row->render);
    row->render=malloc(row->size+tabs*(KILO_TAB_STOP-1)+1);//tab 本身算一个字节，只需要额外多分配7个
    int idx=0;
    for ( i = 0; i < row->size; i++)
    {
       if(row->chars[i]=='\t')
       {
            row->render[idx++]=' ';
            while (idx%(KILO_TAB_STOP)!=0) row->render[idx++]=' ';
       }else{
            row->render[idx++]=row->chars[i];
       }
    }
    row->render[idx]='\0';
    row->rsize=idx;
}
void editorAppendRow(char *s,size_t len){
    E.row=realloc(E.row,sizeof(erow)*(E.numrows+1));
    int at=E.numrows;
    E.row[at].size=len;
    E.row[at].chars =malloc(len+1);
    memcpy(E.row[at].chars,s,len);//not copy \0
    E.row[at].chars[len]='\0';
    E.row[at].render=NULL;
    E.row[at].rsize=0;
    editorUpdateRow(&E.row[at]);
    E.numrows++;
}

/// @brief 在指定行的指定位置插入一个字符
/// @param row 插入的行
/// @param at 插入行的位置
/// @param c 插入的字符
void editorRowInsertChar(erow* row,int at,int c){
    if(at<0||at>row->size) at=row->size;
    row->chars=realloc(row->chars,row->size+2);//row.size 不包含/0所以➕char➕/0
    //memmove能正确处理重叠内存的复制,(dest,src,size)
    memmove(&row->chars[at+1],&row->chars[at],row->size-at+1);
    row->size++;
    row->chars[at]=c;
    editorUpdateRow(row);
}
/*** 编辑操作 ***/
/// @brief 真正调用的插入函数
/// @param c 插入字符
void editorInsertChar(int c){
    if(E.cy==E.numrows){
        editorAppendRow("",0);
    }
    editorRowInsertChar(&E.row[E.cy],E.cx,c);
    E.cx++;
}

/*file i/o*/
void editorOpen(char* filename){
    free(E.filename);
    E.filename=strdup(filename);
    FILE* fp=fopen(filename,"r");
    if(!fp)die("fopen");
    char* line=NULL;
    size_t linecap=0;
    ssize_t linelen;
    while ((linelen=getline(&line,&linecap,fp))!=-1)
    {
        while (linelen>0 && (line[linelen-1]=='\n'||line[linelen-1]=='\r'))
        { linelen--;}
        editorAppendRow(line,linelen);
    }
    free(line);
    fclose(fp); 
}
/// @brief 将 erow 结构数组转换为一个字符串
/// @param buflen 字符串长度
/// @return 文件头指针
char* editorRowsToString(int* buflen){
    int totlen=0;
    int i;
    for ( i = 0; i < E.numrows; i++)
    {
        totlen+=E.row[i].size+1;
    }
    *buflen=totlen;
    char* buf=malloc(totlen);
    char* p=buf;
    for(i=0;i<E.numrows;i++){
        memcpy(p,E.row[i].chars,E.row[i].size);
        p+=E.row[i].size;
        *p='\n';
        p++;//写入\n 向后一跳
    }
    return buf;
}
/// @brief 保存文件到磁盘
void editorSave(){
    if(E.filename==NULL)return;
    int len;
    char* buf=editorRowsToString(&len); 
    // 打开文件：
    // - O_RDWR: 以读写模式打开
    // - O_CREAT: 如果文件不存在则创建
    // - 0644: 文件权限（所有者读写，组和其他用户只读）
    int fd=open(E.filename,O_RDWR|O_CREAT,0644);//fd文件描述符
    ftruncate(fd,len);  // 实际上应该先截断为 0，再写入新内容
    write(fd,buf,len);
    close(fd);
    free(buf);
}
/// @brief 垂直滚动控制，rowoff为偏移量指向当前文件顶部行数，cy为屏幕绝对行数
void editorScroll(){
    E.rx=0;
    if(E.cy<E.numrows){
        E.rx=editorRowCxToRx(&E.row[E.cy],E.cx);
    }
    if (E.cy<E.rowoff)
    {
        E.rowoff=E.cy;
    }
    if(E.cy>=E.rowoff+E.screenrows)
    {
        E.rowoff=E.cy-E.screenrows+1;
    }
    if(E.rx<E.coloff)
    {
        E.coloff=E.rx;
    }
    if(E.rx>E.coloff+E.screencols)
    {
        E.coloff=E.rx-E.screencols+1;
    }
}

/// @brief 设置状态栏消息，支持格式化字符串
/// @param fmt 格式化字符串
/// @param  ... 可变参数列表，对应 fmt 中的占位符
void editorSetStatusMessage(const char*fmt,...){
    va_list ap;
    va_start(ap,fmt);//init ap,fmt is  the fixed end item 
    vsnprintf(E.statusmsg,sizeof(E.statusmsg),fmt,ap);
    va_end(ap);
    E.statusmsg_time=time(NULL);
}
void disenableRawMode()
{
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios)==-1)
    die("tcsetattr");
}
void enableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
    //get keyborad property into struct
    atexit(disenableRawMode);// exit call disenableRawMode
    struct termios raw=E.orig_termios;//new terminal //ICANON:press "enter" transmit sginal.
    /*
    ISIG:close the Ctrl+C、Ctrl+Z sginal
    IXON:input flag
    IEXTEN:implementation-defined extensions
    */
    raw.c_iflag &=~(BRKINT|ICRNL|IXON|ISTRIP|INPCK);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8); 
    raw.c_lflag &=~(ECHO|ICANON|ISIG|IEXTEN );//close the echo,the echo is return display(no show in screen)
    raw.c_cc[VMIN]=0;//响应单个按键（不需要凑够 N 个字符才处理）
    raw.c_cc[VTIME]=10;//1s 1s内没有响应则返回; 100ms 设置为1 buffer
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
    //TCSAFLUSH:waits for all pending  
    //output to be written to the terminal,discards any input that hasn’t been read.
}
/* input */
int editorReadKey(){
    int nread;
    char c;
    while((nread = read(STDIN_FILENO,&c,1))!=1){
        if(nread==-1 && errno != EAGAIN) die("read");
    }
    if ('\x1b'==c)
    {
        char seq[3];
        if(read(STDIN_FILENO,&seq[0],1)!=1) return '\x1b';
        if(read(STDIN_FILENO,&seq[1],1)!=1) return '\x1b';
        if('['==seq[0]){
            if (seq[1]>='0'&&seq[1]<='9')
            {
                if(read(STDIN_FILENO,&seq[2],1)!=1) return '\x1b';
                if (seq[2]=='~')
                {
                    switch (seq[1])
                    {   
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5':return PAGE_UP;
                        case '6':return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                } 
            }else
            {
                switch (seq[1])
                {
                case 'A': return ARROW_UP ;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
                default:
                    break;
                }
            } 
        }else if(seq[0] == 'O'){
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }
        return '\x1b';
    }
    else{
        return c;
    }  
}

int getCursorPosition(int* rows, int* cols){
    char buf[32];
    unsigned int i=0;
    if(write(STDOUT_FILENO,"\x1b[6n", 4) != 4) return -1;//\x1b[6n query cursor position
    while (i<sizeof(buf)-1)
    {
        if(read(STDIN_FILENO,&buf[i],1)!=1) break;
        if (buf[i]=='R')break;
        i++;
    }
    buf[i]='\0';
    if(buf[0]!='\x1b'||buf[1]!='[') return -1;
    if(sscanf(&buf[2],"%d,%d",rows,cols)!=2) return -1;
    //printf("\r\n&buf[1]: '%s'\r\n", &buf[1]);
    //editorReadKey();
    return 0;
}
void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    // //行号显示
    // char rowNumber[16];
    // int rnLen = snprintf(rowNumber, sizeof(rowNumber), "%3d ", y+E.rowoff+ 1);
    // abAppend(ab, rowNumber, rnLen);//行号显示占位 计算rowcol需要减
    int filerow=y+E.rowoff;//absulte postion
    if(filerow>=E.numrows){
        if (E.numrows==0 && y == E.screenrows / 3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
            "Kilo editor -- version %s", KILO_VERSION);
        if (welcomelen > E.screencols) welcomelen = E.screencols;
        int padding = (E.screencols - welcomelen) / 2;
        if (padding) {
            abAppend(ab, "~", 1);
            padding--;
        }
        while (padding--) abAppend(ab, " ", 1);
        abAppend(ab, welcome, welcomelen);
        } else {
        abAppend(ab, "~", 1);
        }
    }else{
        int len=E.row[filerow].rsize-E.coloff;
        if(len<0)len=0;
        if(len>E.screencols) len=E.screencols;
        abAppend(ab,&E.row[filerow].render[E.coloff],len);
    }  
    abAppend(ab, "\x1b[K", 3);
    //if (y < E.screenrows - 1) {最后一行显示状态
      abAppend(ab, "\r\n", 2);
    //}
  }
}
/// @brief 绘制导航栏
/// @param ab 缓存对象
void editorDrawStatusBar(struct abuf* ab){
    abAppend(ab,"\x1b[7m",4);
    char status[80],rstatus[80];
    int len=snprintf(status,sizeof(status),"%.20s-%d lines",E.filename?E.filename:"[NO NAME]",E.numrows);
    int rlen=snprintf(rstatus,sizeof(rstatus),"%d/%d",E.cy+1,E.numrows);
    if(len>E.screencols) len=E.screencols;
    abAppend(ab,status,len);
    while (len<E.screencols)
    {
        if(E.screencols-len==rlen){
            abAppend(ab,rstatus,rlen);
            break;
        }
        else{
            abAppend(ab," ",1);
            len++;
        }
    }
    abAppend(ab,"\x1b[m",3);
    abAppend(ab,"\r\n",2);
}
/// @brief 绘制消息状态栏
/// @param ab 缓存消息
void editorDrawMessageBar(struct abuf *ab){
    abAppend(ab,"\x1b[K",3);//<esc>[K clear the message bar 
    int msglen=strlen(E.statusmsg);
    if(msglen>E.screencols)msglen=E.screencols;
    if(msglen&&time(NULL)-E.statusmsg_time<5){
        abAppend(ab,E.statusmsg,msglen);
    }
}

int getWindowSize(int* rows,int* cols ){
    struct winsize ws;
    if(ioctl(STDIN_FILENO,TIOCGWINSZ,&ws)==-1 || ws.ws_col==0) {
        if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B",12)!=12) return -1; 
        //editorReadKey();// columns wide and the number of rows high the terminal
        return getCursorPosition(rows, cols);   //is into the given winsize struct       
    }else{
        *cols=ws.ws_col;
        *rows=ws.ws_row;
        return 0;
    }
    
}
void editorProcessKeyPress(){
    int c=editorReadKey();
    switch (c)
    {
    case '\r':
        /* TODO */
        break;
    case CTRL_KEY('o'):
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;
    case HOME_KEY:
        E.cx=0;
        break;
    case END_KEY:
        if (E.cy<E.numrows)
        {
            E.cx=E.row[E.cy].size;
        }
        break;
    case BACKSPACE:
    case DEL_KEY:
    case CTRL_KEY('h'):
        /* TODO */
        break;
    case PAGE_DOWN:
    case PAGE_UP:
        {
            if (c==PAGE_UP)
            {
                E.cy=E.rowoff;
            }
            else if(c==PAGE_DOWN)
            {
                E.cy=E.rowoff+E.screenrows-1;
            }
            if(E.cy>E.numrows) E.cy=E.numrows;
            int times=E.screenrows;
            while(times--){
                editorMoveCursor(c==PAGE_UP?ARROW_UP:ARROW_DOWN);//run fast not see cursor skip
            }
        }
        break;
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
    case ARROW_UP:
        editorMoveCursor(c);
        break;
    case CTRL_KEY('l'):
    case '\x1b':
        break;
    default:
        editorInsertChar(c);
        break;
    }
}
void editorRefreshScreen(){
    editorScroll();
    struct abuf ab=ABUF_INIT;
    abAppend(&ab,"\x1b[?25l",6);
    //abAppend(&ab,"\x1b[2J",4);
    //write(STDOUT_FILENO,"\x1b[2J",4);//"\x1b[2J"==="ESC CSI clear the entire screen"
    abAppend(&ab,"\x1b[H",3);
    //write(STDOUT_FILENO,"\x1b[H",3);
    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);
    char buf[32];
    snprintf(buf,sizeof(buf),"\x1b[%d;%dH",E.cy-E.rowoff+1,E.rx-E.coloff+1);
    abAppend(&ab,buf,strlen(buf));
    //abAppend(&ab,"\x1b[H",3);//h command with arguments
    abAppend(&ab,"\x1b[?25h",6);
    write(STDOUT_FILENO,ab.b,ab.len);
    abFree(&ab);
    //write(STDOUT_FILENO,"\x1b[H",3);

}
void initEditor(){
    E.cx=0;
    E.cy=0;
    E.rx=0;
    E.rowoff=0;
    E.coloff=0;
    E.numrows=0;
    E.row=NULL;
    E.filename=NULL;
    E.statusmsg[0]='\0';
    E.statusmsg_time=0;
    if(getWindowSize(&E.screenrows,&E.screencols)==-1) die("getWindowSize");
    E.screenrows-=2;
}
void editorMoveCursor(int key){
    erow* row=(E.cy>=E.numrows)?NULL:&E.row[E.cy];//Boundary Check
    switch (key)
    {
    case ARROW_LEFT:
        if (E.cx!=0)
        {
            E.cx--;
        }else if(E.cy>0){
            E.cy--;
            E.cx=E.row[E.cy].size;
        }
        break;
    case ARROW_RIGHT:
        if (row&&E.cx<row->size)
        {
            E.cx++;
        }else if(row&&E.cx==row->size){
            E.cy++;
            E.cx=0;
        }
        break;
    case ARROW_UP:
        if (E.cy!=0)
        {
           E.cy--;
        }
        break;
    case ARROW_DOWN:
        if (E.cy<E.numrows)
        {
          E.cy++;
        }
        break;
    }
    row =(E.cy>=E.numrows)?NULL:&E.row[E.cy];
    int rowlen= row ? row->size:0;
    if(E.cx>rowlen)
    {
        E.cx=rowlen;
    }

}
int main(int argc,char* argv[])
{
    enableRawMode();
    initEditor();
    if(argc>1){
        editorOpen(argv[1]);
    } 
    editorSetStatusMessage("HELP:ctrl-L=quit");
    while (1){
        editorRefreshScreen();
        editorProcessKeyPress();
        /*
        char c='\0';//read 1 byte from the standard input into the variable c//q is quit
        if(read(STDIN_FILENO,&c,1)==-1 && errno !=EAGAIN ) die("read");
        //read(STDIN_FILENO, &c, 1);
        if(iscntrl(c)){ //tests whether a character is a control character. 
            printf("%d\r\n",c);
        }else{
             printf("%d('%c')\r\n",c,c);//escape sequence
        }
        if(CTRL_KEY('z')==c) break; //realize the ctrl+a exit*/
    };  
                                
    return 0;
}