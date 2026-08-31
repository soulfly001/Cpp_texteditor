#include<termios.h>
#include<unistd.h>//unix header
#include<stdlib.h>
#include<ctype.h>
#include<stdio.h>
#include<errno.h>
#include<string.h>
#include<sys/ioctl.h>
/* define*/
#define CTRL_KEY(k) ((k)& 0x1f) //get low 5bit ,changed "ctrl + k"
#define KILO_VERSION "0.0.1"
/*function declare*/
void editorMoveCursor(int  key); 
enum editorKey{

    ARROW_LEFT=1000,
    ARROW_RIGHT=1001,
    ARROW_UP=1002,
    ARROW_DOWN=1003,
    PAGE_UP=1004,
    PAGE_DOWN=1005,
};
struct editConfig
{
    struct termios orig_termios;
    int screenrows;
    int screencols;
    int cx,cy;//cursor position
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
                        case '5':return PAGE_UP;
                        case '6':return PAGE_DOWN;
                    }
                } 
            }else
            {
                switch (seq[1])
                {
                case 'A':return ARROW_UP ;
                case 'B':return ARROW_DOWN;
                case 'C':return ARROW_RIGHT;
                case 'D':return ARROW_LEFT;
                default:
                    break;
                }
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
void editorDrawRows(struct abuf* ab){
    int y;
    for(y=0;y<E.screenrows;y++){
        if (y==E.screenrows/3)
        {
            char welcome[80];
            int welcomelen=snprintf(welcome,sizeof(welcome),
            "kilo editor -- version %s",KILO_VERSION);
            if(welcomelen>E.screencols)welcomelen=E.screencols;
            int padding =(E.screencols-welcomelen)/2;
            if(padding){
                abAppend(ab,"    ~",5);
                padding--;
            }
            while (padding--) abAppend(ab, " ", 1);
            abAppend(ab,welcome,welcomelen);/*truncate the length of the string in case */        
            abAppend(ab, "\r\n", 2);/*the terminal is too tiny to fit our welcome message*/
        }
        else
        {
            char buf[32];
            int len = snprintf(buf, sizeof(buf), "%3d ~\r\n", y + 1);//return value is write length
            abAppend(ab,buf,len);
        }
        abAppend(ab,"\x1b[K",3);
            //write(STDOUT_FILENO,buf,len);
        if(y<E.screenrows-1)
        {
            abAppend(ab, "\r\n", 2);
        }
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
    case CTRL_KEY('l'):
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;

    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
    case ARROW_UP:
        editorMoveCursor(c);
        break;
    default:
        break;
    }
}
void editorRefreshScreen(){
    struct abuf ab=ABUF_INIT;
    abAppend(&ab,"\x1b[?25l",6);
    //abAppend(&ab,"\x1b[2J",4);
    //write(STDOUT_FILENO,"\x1b[2J",4);//"\x1b[2J"==="ESC CSI clear the entire screen"
    abAppend(&ab,"\x1b[H",3);
    //write(STDOUT_FILENO,"\x1b[H",3);
    editorDrawRows(&ab);
    char buf[32];
    snprintf(buf,sizeof(buf),"\x1b[%d;%dH",E.cy+1,E.cx+1);
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
    if(getWindowSize(&E.screenrows,&E.screencols)==-1) die("getWindowSize");
}
void editorMoveCursor(int key){
    switch (key)
    {
    case ARROW_LEFT:
        if (E.cx!=0)
        {
            E.cx--;
        }
        break;
    case ARROW_RIGHT:
        if (E.cx!=E.screencols-1)
        {
            E.cx++;
        }
        break;
    case ARROW_UP:
        if (E.cy!=0)
        {
           E.cy--;
        }
        break;
    case ARROW_DOWN:
        if (E.cy!=E.screenrows-1)
        {
          E.cy++;
        }
        break;
    }
}
int main()
{
    enableRawMode();
    initEditor();
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