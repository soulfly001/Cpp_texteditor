# Kilo Editor Performance Benchmark Log

## Test Environment
- **Date**: 2026-09-11
- **Platform**: WSL (Windows Subsystem for Linux)
- **Compiler**: gcc -Wall -Wextra -pedantic -std=c99

##  Test 

### Test File
- **Filename**: test_large.c
- **Size**: 1.8M
- **Lines**: 20001 rows

### Performance Metrics
| Metric | before-Value |after-Value |finnal-Value |
|--------|--------------|------------|------------|
| **Load Time** | 123.657 ms |105.769 ms|63.409 ms|
| **Throughput** | ~162,000 rows/sec |~189,000 rows/sec |~317,000 rows/sec |

### Command Used
```bash
./kilo --benchmark test_large.c
```

### 修改内容
直接追加到末尾，消除 ```memmove```. ```editorOpen()```复用了```editorInsertRow().```<br>
```editorInsertRow```包含```memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));```<br>
```E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
        if (!E.row) die("realloc");
        // 获取新行的指针
        erow *row = &E.row[E.numrows];
        // 填充原始字符数据
        row->size = linelen;
        row->chars = malloc(linelen + 1);
        if (!row->chars) die("malloc");
        memcpy(row->chars, line, linelen);
        row->chars[linelen] = '\0';
        // 暂时不计算渲染和高亮，留到最后统一处理
        row->render = NULL;
        row->rsize = 0;
        row->hl = NULL;
        E.numrows++;
    }
    free(line);
    fclose(fp);
```
频繁的 realloc：每次只增加 1 个 erow 的空间，导致系统调用极其频繁，且容易产生内存碎片。  
| Modify-I before | Modify-I after |
| --- | --- |
| <pre><code>E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1))</code></pre> | <pre><code>if (E.numrows == E.row_capacity) {<br>&nbsp;&nbsp;&nbsp;&nbsp;E.row_capacity = E.row_capacity == 0 ? 16 : E.row_capacity * 2;<br>&nbsp;&nbsp;&nbsp;&nbsp;E.row = realloc(E.row, sizeof(erow) * E.row_capacity);<br>}</code></pre> |
| Modify-II before | Modify-II after |
| <pre><code>// 每插入一行，立即计算语法高亮<br>editorInsertRow(E.numrows, line, linelen);<br>// ↓ 内部调用<br>editorUpdateRow(&amp;E.row[at]);&nbsp;&nbsp;// 处理 tab 展开<br>editorUpdateSyntax(row);&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;// 遍历每个字符，匹配关键字</code></pre> | <pre><code>// 加载时：只存储原始数据，不计算高亮<br>row-&gt;render = NULL;<br>row-&gt;rsize = 0;<br>row-&gt;hl = NULL;<br><br>// 文件全部读取完后，再统一计算<br>for (int i = 0; i &lt; E.numrows; i++) {<br>&nbsp;&nbsp;&nbsp;&nbsp;editorUpdateRow(&amp;E.row[i]);<br>}</code></pre> |
