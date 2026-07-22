/* Favorites comment sidecar: resolver VA 0x1347c and XML callbacks
 * VAs 0x28e44/0x295c0.  Entries remain active for 400 centiseconds. */
#include "gotube.h"

#define COMMENT_MAX 512
static GTComment comments[COMMENT_MAX];
static int comment_count;

static unsigned int mail_color(const char *mail)
{
    static const struct { const char *name; unsigned int color; } map[] = {
        {"red",0xff0000ff},{"pink",0xff8080ff},{"orange",0xff00ccff},
        {"yellow",0xff00ffff},{"yelow",0xff00ffff},{"green",0xff00ff00},
        {"cyan",0xffffff00},{"blue",0xffff0000},{"purple",0xffff00c0},
        {"white2",0xff90cccc},{"niconicowhite",0xff90cccc},
        {"red2",0xff3300cc},{"truered",0xff3300cc},
        {"passionorange",0xff0066ff},{"orange2",0xff0066ff},
        {"madyellow",0xff009999},{"yellow2",0xff009999},
        {"elementalgreen",0xff66cc00},{"green2",0xff66cc00},
        {"marineblue",0xfffcff33},{"blue2",0xfffcff33},
        {"nobleviolet",0xffcc3366},{"purple2",0xffcc3366},
        {"black",0xff000000},{NULL,0}
    };
    int i;
    for (i=0; map[i].name; i++)
        if (strstr(mail, map[i].name)) return map[i].color;
    return 0xffffffff;
}

static void text_copy(char *out, int cap, const char *a, const char *b)
{
    int n = 0;
    while (a < b && n + 1 < cap) {
        if (!strncmp(a,"&amp;",5)) { out[n++]='&'; a+=5; }
        else if (!strncmp(a,"&lt;",4)) { out[n++]='<'; a+=4; }
        else if (!strncmp(a,"&gt;",4)) { out[n++]='>'; a+=4; }
        else if (!strncmp(a,"&quot;",6)) { out[n++]='\"'; a+=6; }
        else out[n++]=*a++;
    }
    out[n]=0;
}

static int cmp_comment(const void *a, const void *b)
{
    return ((const GTComment *)a)->vpos - ((const GTComment *)b)->vpos;
}

int go_comments_load_for_media(const char *path)
{
    char side[520], *xml, *p; SceUID fd; SceOff size;
    comment_count = 0;
    if (!path) return 0;
    go_sidecar_path(path, ".xml", side, sizeof(side));
    fd=sceIoOpen(side,PSP_O_RDONLY,0); if(fd<0) return 0;
    size=sceIoLseek(fd,0,PSP_SEEK_END); sceIoLseek(fd,0,PSP_SEEK_SET);
    if(size<1 || size>1024*1024){sceIoClose(fd);return 0;}
    xml=malloc((int)size+1); if(!xml){sceIoClose(fd);return 0;}
    if(sceIoRead(fd,xml,(int)size)!=size){free(xml);sceIoClose(fd);return 0;}
    sceIoClose(fd); xml[size]=0; p=xml;
    while(comment_count<COMMENT_MAX && (p=strstr(p,"<chat"))) {
        char *tag_end=strchr(p,'>'), *end; GTComment *c; char mail[160]="";
        char *v=strstr(p,"vpos=\"");
        if(!tag_end || !(end=strstr(tag_end+1,"</chat>"))) break;
        c=&comments[comment_count]; memset(c,0,sizeof(*c)); c->color=0xffffffff;
        if(v && v<tag_end) c->vpos=atoi(v+6);
        v=strstr(p,"mail=\"");
        if(v && v<tag_end){char *q=strchr(v+6,'\"');int n=q?q-(v+6):0;if(n>159)n=159;memcpy(mail,v+6,n);mail[n]=0;}
        c->color=mail_color(mail); c->position=strstr(mail,"shita")?1:(strstr(mail,"ue")?2:0);
        c->size=strstr(mail,"small")?1:(strstr(mail,"big")?2:0);
        text_copy(c->text,sizeof(c->text),tag_end+1,end);
        if(c->text[0]) comment_count++;
        p=end+7;
    }
    free(xml); qsort(comments,comment_count,sizeof(comments[0]),cmp_comment);
    return comment_count;
}

int go_comments_count(void) { return comment_count; }
const GTComment *go_comments_get(int index)
{ return index>=0 && index<comment_count ? &comments[index] : NULL; }
