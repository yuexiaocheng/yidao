#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <curl/curl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#define MAX_ONE_PAGE (2*1024*1024)
#define MAX_ONE_CHAPTER (64*1024)
#define cpymem(dst, src, n)   (((char *) memcpy(dst, src, n)) + (n))

int g_url_num = 0;
char g_urls[10240][32];

void* memstr(const void* str, size_t n, char* r, size_t rn) {
    unsigned const char* s = str;
    while (n >= rn) {
        if (0 == memcmp(s, r, rn)) 
            return (void*)s;
        s++;
        n--;
    }
    return (void*)0;
}

int mkdir_r(char* path) {
    char dir[256] = {0};
    char* p = NULL;
    char* cur = NULL;
    int ret = 0;

    cur = path;
    if ('/' != *cur && '.' != *cur)
        return -1;

    if (0 == access(path, F_OK))
        return 0;
    cur++;
    p = strchr(cur, '/');
    while(NULL != p) {
        memcpy(dir, path, p-path+1);
        if (0 != access(dir, F_OK)) {
            // not exist, create it
            ret = mkdir(dir, 0777);
            if (ret < 0) {
                printf("%s(%d): mkdir %s failed\n", __FUNCTION__,
                        __LINE__, dir);
                return -2;
            }
        }
        cur = p + 1;
        if ((cur - path) > (int)strlen(path))
            break;
        p = strchr(cur, '/');
    }
    return 0;
}

typedef struct {
    int max_len;
    char* cur;
    char* data;
} page_s;

static size_t write_data(void* p, size_t size, size_t nmemb, void* page) {
    size_t sz = size*nmemb;
    page_s* pg = (page_s*)page;
    pg->cur = cpymem(pg->cur, p, sz);
    return sz;
}

int main(void)
{
    CURL *curl;
    CURLcode res;
    page_s pg;
    int ret_code = 0;
    int ret = 0;
    char* p1 = NULL, *p2 = NULL, *p3 = NULL;
    char* tmp = NULL, *zjt = NULL;
    char page_next[256];
    char title[256];
    char* chapter = NULL;
    int i;
    char path[256];
    FILE* f = NULL, *f2 = NULL;
    char* line = NULL;
    size_t len = 0;
    ssize_t read = 0;
    char tmps[1024];
	int exist = 0;
	int changed = 0;

    const char* base_url = "http://tieba.baidu.com";
    const char* neirong = "./html_template/neirong.html";
    const char* zhangjie = "./html_template/zhangjie.html";

    char* zj = (char*)malloc(MAX_ONE_PAGE);
    if (NULL == zj) {
        printf("%s(%d): malloc %d failed\n", __FUNCTION__, __LINE__, MAX_ONE_PAGE);
        return -1;
    }
    pg.max_len = MAX_ONE_PAGE;
    pg.cur = pg.data = (char*)malloc(pg.max_len);
    if (NULL == pg.data) {
        printf("%s(%d): malloc %d failed\n", __FUNCTION__, __LINE__, pg.max_len);
        return -1;
    }
    chapter = (char*)malloc(MAX_ONE_CHAPTER);
    if (NULL == chapter) {
        printf("%s(%d): malloc %d failed\n", __FUNCTION__, __LINE__, MAX_ONE_CHAPTER);
        return -1;
    }

    ret = snprintf(page_next, sizeof(page_next)-1, "%s%s", base_url, "/f/good?kw=%CE%CA%BE%B5&cid=1&tp=0&pn=0");
    page_next[ret] = '\0';
    curl = curl_easy_init();
    if(curl) {
        while(strlen(page_next) > 0) {
            curl_easy_setopt(curl, CURLOPT_URL, page_next);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &pg);
            res = curl_easy_perform(curl);
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &ret_code);
            if(res != CURLE_OK)
                printf("%s(%d): curl_easy_perform() failed: %s\n", 
                        __FUNCTION__, __LINE__, curl_easy_strerror(res));
            if (200 != ret_code) {
                printf("%s(%d): code=%d, something is wrong!\n", __FUNCTION__, __LINE__, ret_code);
            }

            // page is complete, parse it
            p1 = pg.data;
            do {
                p2 = memstr(p1, pg.cur-p1, "<a href=\"/p/", sizeof("<a href=\"/p/")-1);
                if (p2) {
                    p2 += 9; // now point `/`
                    p3 = memchr(p2, '"', pg.cur-p2);
                    if (NULL == p3) {
                        printf("%s(%d): don't find \"\n", __FUNCTION__, __LINE__);
                        return -2;
                    }
                    tmp = cpymem(g_urls[g_url_num++], p2, p3-p2);
                    *tmp++ = '\0';
                    p1 = p3+1;
                }
            } while (p2);
            p1 = memstr(pg.data, pg.cur-pg.data, "class=\"next\">", sizeof("class=\"next\">")-1);
            if (NULL == p1) {
                // no more, finished
                printf("%s(%d): get %d urls, great!\n", __FUNCTION__, __LINE__, g_url_num);
                break;
            }
            else {
                // get the next page url
                page_next[0] = '\0';
                pg.cur = pg.data;
                p2 = memrchr(pg.data, '"', p1-pg.data);
                if (p2) {
                    p3 = memrchr(pg.data, '"', p2-1-pg.data);
                    if (p3) {
                        tmp = cpymem(page_next, base_url, strlen(base_url));
                        tmp = cpymem(tmp, p3+1, p2-p3); *tmp++ = '\0';
                    }
                }
            }
        }
        // now get every page content
		exist = 0;
		changed = 0;
        zjt = zj;
        for (i=g_url_num-1; i>=0; --i) {
			// open template file
			ret = snprintf(path, sizeof(path)-1, ".%s.html", g_urls[i]);
			path[ret] = '\0';
			mkdir_r(path);
			// already exist, just continue
			if (0 == access(path, F_OK)) {
				exist++;
				continue;
			}
            pg.cur = pg.data;
            ret = snprintf(page_next, sizeof(page_next)-1, "%s%s?see_lz=1", base_url, g_urls[i]);
            page_next[ret] = '\0';
            curl_easy_setopt(curl, CURLOPT_URL, page_next);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &pg);
            res = curl_easy_perform(curl);
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &ret_code);
            if(res != CURLE_OK)
                printf("%s(%d): curl_easy_perform() failed: %s\n", 
                        __FUNCTION__, __LINE__, curl_easy_strerror(res));
            if (200 != ret_code) {
                printf("%s(%d): code=%d, something is wrong!\n", __FUNCTION__, __LINE__, ret_code);
            }
            // parse content we need
            p1 = memstr(pg.data, pg.cur-pg.data, "<title>", sizeof("<title>")-1);
            if (p1) {
                p1 += (sizeof("<title>")-1);
                p2 = memstr(p1, pg.cur-p1, "</title>", sizeof("</title>")-1);
                if (p2) {
                    tmp = cpymem(title, p1, p2-p1); *tmp++ = '\0';
                    tmp = chapter;
                    p3 = pg.data;
                    do {
                        p1 = memstr(p3, pg.cur-p3, 
                                "class=\"d_post_content j_d_post_content\">", 
                                sizeof("class=\"d_post_content j_d_post_content\">")-1);
                        if (p1) {
                            p1 += (sizeof("class=\"d_post_content j_d_post_content\">")-1);
                            p2 = memstr(p1, pg.cur-p1, "</div>", sizeof("</div>")-1);
                            tmp = cpymem(tmp, p1, p2-p1);
                            p3 = p2 + 1;
                        }
                    } while (p1);
                    *tmp++ = '\0';
                    // record href
                    ret = snprintf(tmps, sizeof(tmps)-1, 
                            "<div class=\"listu\"><a href=\"%s.html\" title=\"%s\">%s</a></div>\n", 
                            g_urls[i], title, title);
                    tmps[ret] = '\0';
                    zjt = cpymem(zjt, tmps, strlen(tmps));
					// get all, now write static page
					// printf("%s\n%s\n", title, chapter);
					f = fopen(neirong, "r");
					if (NULL == f)
                    {
                        printf("%s(%d): fopen(%s) failed, error(%d):%s\n", 
                                __FUNCTION__, __LINE__, neirong, errno, strerror(errno));
                        return -8;
                    }
                    f2 = fopen(path, "w");
                    if (NULL == f2)
                    {
                        printf("%s(%d): fopen(%s) failed, error(%d):%s\n", 
                                __FUNCTION__, __LINE__, path, errno, strerror(errno));
                        return -9;
                    }
					changed++;
                    while (-1 != (read = getline(&line, &len, f))) {
                        if (0 == memcmp(line, "$title$", sizeof("$title$")-1))
                        {
                            fwrite(title, 1, strlen(title), f2);
                        }
                        else if (0 == memcmp(line, "$content$", sizeof("$content$")-1))
                        {
                            fwrite(chapter, 1, strlen(chapter), f2);
                        }
                        else if (0 == memcmp(line, "$next_page$", sizeof("$next_page$")-1))
                        {
                            if (i-1 >= 0) {
                                ret = snprintf(tmps, sizeof(tmps)-1, 
                                        "<p class=\"t\"><a href=\"../zhangjie.html\">Index</a>"
                                        "|<a href=\"%s.html\">Next</a></p>", 
                                        g_urls[i-1]);
                            }
                            else {
                                ret = snprintf(tmps, sizeof(tmps)-1, 
                                        "<p class=\"t\"><a href=\"../zhangjie.html\">Index</a></p>");
                            }
                            tmps[ret] = '\0';
                            fwrite(tmps, 1, strlen(tmps), f2);
                        }
                        else
                        {
                            fwrite(line, 1, strlen(line), f2);
                        }
                    }
                    fclose(f);
                    f = NULL;
                    fclose(f2);
                    f2 = NULL;
                }
            }
        }
        *zjt++ = '\0';
		printf("%s(%d): already exist %d, changed %d, done!\n", 
				__FUNCTION__, __LINE__, exist, changed);
        // create zhangjie
        f = fopen(zhangjie, "r");
        if (NULL == f)
        {
            printf("%s(%d): fopen(%s) failed, error(%d):%s\n", 
                    __FUNCTION__, __LINE__, zhangjie, errno, strerror(errno));
            return -8;
        }
        ret = snprintf(path, sizeof(path)-1, "./%s", "zhangjie.html");
        path[ret] = '\0';
        mkdir_r(path);
        f2 = fopen(path, "w");
        if (NULL == f2)
        {
            printf("%s(%d): fopen(%s) failed, error(%d):%s\n", 
                    __FUNCTION__, __LINE__, path, errno, strerror(errno));
            return -9;
        }
        while (-1 != (read = getline(&line, &len, f)))
        {
            if (0 == memcmp(line, "$title$", sizeof("$title$")-1))
            {
                fwrite("wenjing", 1, strlen("wenjing"), f2);
            }
            else if (0 == memcmp(line, "$href$", sizeof("$href$")-1))
            {
                fwrite(zj, 1, strlen(zj), f2);
            }
            else
            {
                fwrite(line, 1, strlen(line), f2);
            }
        }
        fclose(f);
        f = NULL;
        fclose(f2);
        f2 = NULL;

        /* always cleanup */ 
        curl_easy_cleanup(curl);
    }
    free(pg.data);
    pg.cur = pg.data = NULL;
    free(chapter);
    chapter = NULL;
    free(zj);
    zj = NULL;
    return 0;
}
