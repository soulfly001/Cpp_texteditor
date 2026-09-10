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
#define KILO_QUIT_TIMES 3
/*function Prototype*/
void editorMoveCursor(int  key); 
void editorRefreshScreen();
void editorSetStatusMessage(const char* fmt,...);
char *editorPrompt(char *prompt, void (*callback)(char *, int));
void editorSelectSyntaxHighLight();
/*             data        */


/// @brief 定义一种文件类型的语法高亮配置
struct editorSyntax
{
    char* filetype;
    char** filematch;
    char** keywords;
    char* singleline_comment_start;
    int flags;
};

typedef struct erow
{
    int size;
    int rsize;//渲染的字符指针size
    char *chars;
    char *render;//实际渲染的字符指针 包含tab
    unsigned char* hl;//heightlight length==rsize,keep every bits colors
}erow;
/*
Backspace 键  发送 127 (DEL)
Delete 键 发送转义序列 ESC [ 3 
Ctrl+H 发送 8 (BS)
*/
//定义颜色结构
typedef struct 
{
    int r,g,b;
    int is_rgb;
    int ansi_code;
}editorColor;
//颜色常量
#define COLOR_WHITE (editorColor){255,255,255,1,0}
#define COLOR_TEXT    (editorColor){0, 0, 0, 0,39}
#define COLOR_CYAN (editorColor){127,255,212,1,0}
#define COLOR(r,g,b) (editorColor){r,g,b,1,0}
//函数声明
editorColor editorSyntaxToColor(int hl);
int colorEquals(editorColor a, editorColor b);
void colorToEscape(editorColor color, char* buf, int bufsize);
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
enum editorHightLight{
    HL_NORMAL=0,
    HL_COMMENT,//single line 
    HL_KEYWORD1,
    HL_KEYWORD2,
    HL_STRING,
    HL_NUMBER,
    HL_MATCH
};
#define HL_HIGHLIGHT_NUMBER (1<<0)//1左移1bit 第0位：高亮数字
#define HL_HIGHLIGHT_STRING  (1<<1)//第1位：高亮string
struct editConfig
{
    struct termios orig_termios;
    struct editorSyntax* syntax;
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
    int dirty; // file modify and no save bit
    int cx,cy;//cursor position cx光标在字符数组中的逻辑索引/下标
};
struct editConfig E;
/********file  types*********/
char* C_HL_extensions[]={".c",".h",".cpp",NULL};
char* C_HL_keywords[]=
{
    "switch", "if", "while", "for", "break", "continue", "return", "else",
  "struct", "union", "typedef", "static", "enum", "class", "case",
  "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
  "void|", NULL
};
struct editorSyntax HLDB[]=
{
   {
        "c",
        C_HL_extensions,
        C_HL_keywords,
        "//",
        HL_HIGHLIGHT_NUMBER|HL_HIGHLIGHT_STRING
   },
};
#define HLDB_ENTRIES (sizeof(HLDB)/sizeof(HLDB[0]))
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
/*function declare*/


void editorUpdateSyntax(erow* row);
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
    editorUpdateSyntax(row);
}
/// @brief 在指定的行索引处插入一行新的文本
/// @param at 要插入的位置索引
/// @param s 要插入的字符串内容
/// @param len 字符串的长度
void editorInsertRow(int at,char *s,size_t len){
    if(at<0||at>E.numrows)return;
    E.row=realloc(E.row,sizeof(erow)*(E.numrows+1));
    memmove(&E.row[at+1],&E.row[at],sizeof(erow)*(E.numrows-at));
    E.row[at].size=len;
    E.row[at].chars =malloc(len+1);
    memcpy(E.row[at].chars,s,len);//not copy \0
    E.row[at].chars[len]='\0';
    E.row[at].render=NULL;
    E.row[at].rsize=0;
    E.row[at].hl=NULL;
    editorUpdateRow(&E.row[at]);
    E.numrows++;
    E.dirty++;
}

    /***       编辑操作       ***/

/// @brief 释放行所持有的指针
/// @param erow 行指针
void editorFreeRow(erow* erow){
    free(erow->render);
    free(erow->chars);
    free(erow->hl);
}
/// @brief deleting a empty row!
/// @param at row at position
void editorDelRow(int at){
    if(at<0||at>E.numrows) return;
    editorFreeRow(&E.row[at]);
    memmove(&E.row[at],&E.row[at+1],sizeof(erow)*(E.numrows-at-1));
    E.numrows--;
    E.dirty++;
}
/// @brief 行合并
/// @param row  前一行指针
/// @param s 被合并行的指针
/// @param length 合并长度
void editorRowAppendString(erow* row,char* s,size_t length){
    row->chars=realloc(row->chars,row->size+length+1);
    memcpy(&row->chars[row->size],s,length);
    row->size+=length;
    row->chars[row->size]='\0';
    editorUpdateRow(row);
    E.dirty++;
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
    E.dirty++;
}

/// @brief 真正调用的插入函数
/// @param c 插入字符
void editorInsertChar(int c){
    if(E.cy==E.numrows){
        editorInsertRow(E.numrows,"",0);
    }
    editorRowInsertChar(&E.row[E.cy],E.cx,c);
    E.cx++;
}
/// @brief 处理回车键，在当前光标位置插入新行（将一行拆分为两行）
void editorInsertNewLine(){
    if(E.cx==0){
        editorInsertRow(E.cy,"",0);
    }else{
        erow* row=&E.row[E.cy];
        editorInsertRow(E.cy+1,&row->chars[E.cx],row->size-E.cx);
        row=&E.row[E.cy];//// 【重要】重新获取当前行指针。
        row->size=E.cx;
        row->chars[row->size]='\0';
        editorUpdateRow(row);
    }
    E.cy++;
    E.cx=0;
}
/// @brief 删除字符
/// @param row 删除的行
/// @param at 删除的位置
void editorRowDelChar(erow* row,int at){
    if(at<0||at>=row->size)return;
    memmove(&row->chars[at],&row->chars[at+1],row->size-at);
    row->size--;
    editorUpdateRow(row);
    E.dirty++;
}

/// @brief 对外删除接口
void editorDelChar(){
    if(E.cy==E.numrows)return;
    if(E.cx==0 &&E.cy==0)return;
    erow* erow=&E.row[E.cy];
    if(E.cx>0){
        editorRowDelChar(erow,E.cx -1);
        E.cx--;
    }else{
        E.cx=E.row[E.cy-1].size;//光标移动到上一行的末尾
        editorRowAppendString(&E.row[E.cy-1],erow->chars,erow->size);
        editorDelRow(E.cy);
        E.cy--;
    }
    
}


/*file i/o*/
void editorOpen(char* filename){
    free(E.filename);
    E.filename=strdup(filename);
    editorSelectSyntaxHighLight();
    FILE* fp=fopen(filename,"r");
    if(!fp)die("fopen");
    char* line=NULL;
    size_t linecap=0;
    ssize_t linelen;
    while ((linelen=getline(&line,&linecap,fp))!=-1)
    {
        while (linelen>0 && (line[linelen-1]=='\n'||line[linelen-1]=='\r'))
        { linelen--;}
        editorInsertRow(E.numrows,line,linelen);
    }
    free(line);
    fclose(fp); 
    E.dirty=0;
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
    if(E.filename==NULL){
        E.filename=editorPrompt("Save as: %s(ESC to cancel)",NULL);
        if(E.filename==NULL){
            editorSetStatusMessage("save aborted");
            return;
        }
        editorSelectSyntaxHighLight();
    }
    int len;
    char* buf=editorRowsToString(&len); 
    // 打开文件：
    // - O_RDWR: 以读写模式打开
    // - O_CREAT: 如果文件不存在则创建
    // - 0644: 文件权限（所有者读写，组和其他用户只读）
    int fd=open(E.filename,O_RDWR|O_CREAT,0644);//fd文件描述符
    if(fd!=-1){
        if(ftruncate(fd,len)!=-1){// 实际上应该先截断为 0，再写入新内容
            if(write(fd,buf,len)==len){
                    close(fd);
                    free(buf);
                    E.dirty=0;
                    editorSetStatusMessage("%d bytes written to disk",len);
                    return;
            }
        }
        close(fd);
    }
    free(buf);
    editorSetStatusMessage("can't save I/O error:%s",strerror(errno));
}
/*****find****/

/**
 * @brief 编辑器查找功能的回调函数，用于在文本中搜索指定的查询字符串。
 * @param query 要搜索的目标字符串（子串）。
 * @param key   用户当前按下的键值（ASCII 码或转义序列）。
 */
/*增量搜索：不需要按enter  键入一个搜一个*/
void editorFindCallback(char* query,int key){
    static int last_match=-1;//记录上次匹配的位置
    static int diretion=1;//1---forward -1 backward
    static int saved_hl_line;//存储被搜索高亮的那一行的行号
    static char* saved_hl=NULL;//不为 NULL 时，指向保存的原始高亮数据
    if(saved_hl){
        erow* row=&E.row[saved_hl_line];
        memcpy(row->hl,saved_hl,row->rsize);
        free(saved_hl);
        saved_hl=NULL;
    }
    if(key=='\r'||key=='\x1b'){
        last_match=-1;
        diretion=1;
        return ;
    }else if(key==ARROW_RIGHT||key==ARROW_DOWN){
        diretion=1;
    }else if(key==ARROW_LEFT||key==ARROW_UP){
        diretion=-1;
    }else{
        last_match=-1;
        diretion=1;
    }
    if(last_match==-1)diretion=1;
    int current=last_match; //现在匹配的位置从上一次的位置开始
    int i;
    for ( i = 0; i < E.numrows; i++)
    {
        current+=diretion;//+1从上次匹配的位置下或上一行搜索
        if(current==-1)current=E.numrows-1;//向上搜越界
        else if(current==E.numrows)current=0;//向下搜越界
        erow* row=&E.row[current];
        char* match=strstr(row->chars,query);//（主串，子串）
        if(match){
            last_match=current;
            E.cy=current;
            E.cx=match-row->chars;
            E.rowoff=E.numrows;// be at the very top of the screen
            saved_hl_line=current;
            saved_hl=malloc(row->rsize);
            memcpy(saved_hl,row->hl,row->rsize);
            memset(&row->hl[match-row->chars],HL_MATCH,strlen(query));
            //match-row->chars:匹配文本在行中的索引偏移量
            break;
        }
    }  
}

/// @brief 查找字符串
//传统搜索：用户输入完整关键词 → 按回车 → 执行搜索
void editorFind(){
    int save_cx=E.cx;
    int save_cy=E.cy;
    int save_coloff=E.coloff;
    int save_rowoff=E.rowoff;
    char* query=editorPrompt("Search: %s (Use ESC/Arrows/Enter)",editorFindCallback);
    if(query){
        free(query);
    }else{
        E.cx=save_cx;
        E.cy=save_cy;
        E.coloff=save_coloff;
        E.rowoff=save_rowoff;
    }
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

/// @brief 回显键入的字符
/// @param prompt 键入字符
/// @param callback 回调函数处理
/// @return 键入字符
char* editorPrompt(char* prompt,void(*callback)(char*,int)){
    size_t buffsize=128;
    char* buf=malloc(buffsize);
    size_t buflen=0;
    while (1)
    {
        editorSetStatusMessage(prompt,buf);//buf ->%s
        editorRefreshScreen();
        int c=editorReadKey();
        if(c==DEL_KEY||c==CTRL_KEY('h')||c==BACKSPACE){
            if(buflen!=0)buf[--buflen]='\0';
        }else if(c=='\x1b'){
            editorSetStatusMessage("");
            if(callback) callback(buf,c);
            free(buf);
            return NULL;
        }
        else if(c=='\r'){
            if (buflen!=0)
            {
                editorSetStatusMessage("");
                if(callback) callback(buf,c);
                return buf;
            }
        } else if (!iscntrl(c)&&c<128)
            {
                if (buflen==buffsize-1)
                {
                    buffsize*=2;
                    buf=realloc(buf,buffsize);
                }
                buf[buflen++]=c;
                buf[buflen]='\0';
            }
            if(callback) callback(buf,c);
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
        int len = E.row[filerow].rsize - E.coloff;
        if (len < 0) len = 0;
        if (len > E.screencols) len = E.screencols;
        char* c = &E.row[filerow].render[E.coloff];
        unsigned char* hl = &E.row[filerow].hl[E.coloff];
        editorColor current_color = COLOR_TEXT;  // 初始化当前颜色为文本默认颜色
        for (int j = 0; j < len; j++) {
        // 2. 获取当前字符应有的颜色（无论是 NORMAL, NUMBER 还是未来的 KEYWORD）
        editorColor new_color = editorSyntaxToColor(hl[j]);
        // 3. 核心优化：只在颜色真正改变时，才输出转义序列
        if (!colorEquals(new_color, current_color)) {
            char buf[32];
            colorToEscape(new_color, buf, sizeof(buf));
            abAppend(ab, buf, strlen(buf));  // 注意是 strlen
            current_color = new_color;       // 更新状态
            }
        abAppend(ab, &c[j], 1);// 4. 输出当前字符
        }
        abAppend(ab, "\x1b[39m", 5);// 5. 行末统一重置为终端默认颜色
    }  
    abAppend(ab, "\x1b[K", 3);// 清除从光标到行尾的内容（防止上一行长内容残留）
    abAppend(ab, "\r\n", 2);    // 换行
  }
}
/// @brief 绘制导航栏
/// @param ab 缓存对象
void editorDrawStatusBar(struct abuf* ab){
    abAppend(ab,"\x1b[7m",4);
    char status[80],rstatus[80];
    int len=snprintf(status,sizeof(status),"%.20s-%d lines %s",
        E.filename?E.filename:"[NO NAME]",E.numrows,E.dirty?"(modified)":"");
    int rlen=snprintf(rstatus,sizeof(rstatus),"%s|%d/%d",
            E.syntax?E.syntax->filetype:"no filetype",E.cy+1,E.numrows);
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

/*syntax highlighting*/

/// @brief 字符是否被视为分隔符
/// @param c 字符
/// @return 
int is_separator(int c){
    return isspace(c)||c=='\0'||strchr(",.()+-/*=~%<>[];",c)!=NULL;
}

/**
 * @brief 更新行的语法高亮信息
 * @param row 指向要更新语法高亮的 erow 结构体的指针
 */
void editorUpdateSyntax(erow* row){
    row->hl =realloc(row->hl,row->rsize);
    memset(row->hl,HL_NORMAL,row->rsize);
    if(E.syntax==NULL) return;
    char** keywords=E.syntax->keywords;
    char* scs=E.syntax->singleline_comment_start;
    int scs_len=scs?strlen(scs):0;
    int prev_sep=1;//判断前一个字符是否是分隔符
    int in_string=0;
    int i=0;
    while (i<row->rsize)//使用while可以手动控制
    {
        char c=row->render[i];
        unsigned char prev_hl=(i>0)?row->hl[i-1]:HL_NORMAL;//小数点前后高亮
        /*single-comment高亮处理逻辑*/
        if(scs_len&&!in_string)
        {
            if(!strncmp(&row->render[i],scs,scs_len))//匹配成功返回0
            {
                memset(&row->hl[i],HL_COMMENT,row->rsize-i);
                break;
            }
        }
        /*string高亮处理逻辑*/
        if(E.syntax->flags&HL_HIGHLIGHT_STRING)
        {
            if(in_string)
            {
                row->hl[i]=HL_STRING;
                if(c=='\\'&&i+1<row->rsize)//处理转义‘\’
                {
                    i++;
                    row->hl[i]=HL_STRING;
                    i++;
                    continue;
                }
                if(c==in_string) in_string=0;
                i++;
                prev_sep=1;
                continue;
            }
            else
            {
                if (c == '"' || c == '\'')
                {
                    in_string=c;
                    row->hl[i]=HL_STRING;
                    i++;
                    continue;
                }
            }
        }
        /*数字高亮处理逻辑*/
        if(E.syntax->flags&HL_HIGHLIGHT_NUMBER)
        {
            if((isdigit(c) && (prev_sep||prev_hl==HL_NUMBER))||(c=='.'&& prev_hl==HL_NUMBER))
            {
                row->hl[i]=HL_NUMBER;
                i++;
                prev_sep=0;
                continue;
            }
        }
        /*关键字高亮处理逻辑*/
        if(prev_sep)
        {
            int j;
            for ( j = 0;keywords[j]; j++)
            {
               int klen=strlen(keywords[j]);
               int kw2=keywords[j][klen-1]=='|';//末尾包含|
               if(kw2) klen--;
               if(!strncmp(&row->render[i],keywords[j],klen)&&
                is_separator(row->render[i+klen])) // 匹配关键字 + 检查后面是否是分隔符
                {
                    memset(&row->hl[i],kw2?HL_KEYWORD2:HL_KEYWORD1,klen);
                    i+=klen;
                    break;
                }
            }
            if (keywords[j]!=NULL)
            {
                prev_sep = 0;
                continue;
            } 
        }
        prev_sep=is_separator(c);
        i++;
    }
}

/// @brief 获取颜色
/// @param hl 
/// @return 颜色结构体
editorColor editorSyntaxToColor(int hl) {
    switch (hl) {
        case HL_NORMAL:  return COLOR_TEXT;
        case HL_COMMENT: return COLOR(0, 255, 154);
        case HL_STRING:  return COLOR(161, 191, 105);
        case HL_KEYWORD1: return COLOR(30, 144, 255);
        case HL_KEYWORD2: return COLOR(255, 218, 185);
        case HL_NUMBER:  return COLOR_CYAN;//127,255,212
        case HL_MATCH:   return COLOR(100,149,237);
        default:         return COLOR_TEXT;
    }
}
/// @brief  判断颜色是否相等
/// @param a 
/// @param b 
/// @return 
int colorEquals(editorColor a, editorColor b) {
    if (a.is_rgb != b.is_rgb) return 0;
    if (a.is_rgb) {
        return a.r == b.r && a.g == b.g && a.b == b.b;
    }
    return a.ansi_code == b.ansi_code;
}
/// @brief 生成转义序列
/// @param color 
/// @param buf 
/// @param bufsize 
void colorToEscape(editorColor color, char* buf, int bufsize) {
    if (color.is_rgb) {
        snprintf(buf, bufsize, "\x1b[38;2;%d;%d;%dm", color.r, color.g, color.b);
    } else {
        snprintf(buf, bufsize, "\x1b[%dm", color.ansi_code);
    }
}
/*
@brief 根据当前文件名自动检测并设置语法高亮规则。
@details 该函数通过遍历全局语法高亮数据库 (HLDB)，尝试为当前打开的文件 (E.filename) 
 *  匹配最合适的语法定义。匹配策略分为两种：
 *  1. 扩展名精确匹配：若匹配模式以 '.' 开头（如 ".c", ".h"），则提取文件扩展名 
 *     并与模式进行精确比较 (strcmp)，避免 ".cpp" 被误匹配为 ".c"。
 *  2. 文件名包含匹配：若匹配模式不以 '.' 开头（如 "Makefile", "CMakeLists"），
 *     则检查完整文件路径/名称中是否包含该子串 (strstr)。
 *  一旦找到首个匹配项，即将对应的语法规则指针赋值给 E.syntax 并立即返回。

*/
void editorSelectSyntaxHighLight()
{
    E.syntax=NULL;
    if(E.filename==NULL) return;
    char* ext=strrchr(E.filename,'.');
    for (unsigned int i = 0; i < HLDB_ENTRIES; i++)
    {
        struct editorSyntax* s=&HLDB[i];
        unsigned int j=0;
        while (s->filematch[j])
        {
            int is_dot=(s->filematch[j][0]=='.');//[.c,.cpp,.h,null]
            // 1. dot 开头(扩展名匹配)
            // 2. 不以 . 开头(文件名包含匹配)
            if((is_dot&&ext&&!strcmp(ext,s->filematch[j]))||
               (!is_dot && strstr(E.filename, s->filematch[j])))
            {
                E.syntax=s;
                for (int filerow = 0; filerow < E.numrows; filerow++)
                {
                    editorUpdateSyntax(&E.row[filerow]);
                } 
            }
            j++;
        }  
    }
}
/* color end */
void editorProcessKeyPress(){
    static int quit_times=KILO_QUIT_TIMES;//程序运行期间，这行代码只执行一次。
    int c=editorReadKey();//quit_times 不会销毁，它静静地待在内存的静态区，保留着当前的值。
    switch (c)
    {
    case '\r':
        editorInsertNewLine();
        break;
    case CTRL_KEY('o'):
        if (E.dirty&&quit_times>0)
        {
            editorSetStatusMessage("WARNING!!! File has unsaved changes. "
            "Press Ctrl-O %d more times to quit",quit_times);
            quit_times--;
            return;
        }
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;
    case CTRL_KEY('s'):
        editorSave();
        break;
    case CTRL_KEY('f'):
        editorFind();
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
        if(c==DEL_KEY)editorMoveCursor(ARROW_RIGHT);//del为删除右侧字符，先做右移再删除
        editorDelChar();
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
    quit_times=KILO_QUIT_TIMES;
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
    E.dirty=0;
    E.syntax=NULL;
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
    editorSetStatusMessage("HELP:ctrl-s=save|ctrl-o=quit|ctrl-f=find");
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