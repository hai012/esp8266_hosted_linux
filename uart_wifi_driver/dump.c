#include "common.h"

char output[4096];
void dump(struct device * dev,const char *buf, int len) {
    int i;
    int offset = 0;
    int ret;
    for (i = 0; i < len; i++) {
        ret = snprintf(output + offset,  sizeof(output) - offset, "0x%02X,", buf[i]);
        if (ret <= 0) {
            dev_err(dev,"recv overflow  %d,%d,%d", i, len,offset);
            break;
        }
        offset += ret;
    }
    output[offset]='\0';
    dev_err(dev,"i=%d,len=%d,%s", i,len,output);
}

char recv_output[4096];
void recv_dump(struct device * dev,const char *buf, int len) {
    int i;
    int offset = 0;
    int ret;
    for (i = 0; i < len; i++) {
        ret = snprintf(recv_output + offset,  sizeof(recv_output) - offset, "0x%02X,", buf[i]);
        if (ret <= 0) {
            dev_err(dev,"recv overflow  %d,%d,%d", i, len,offset);
            break;
        }
        offset += ret;
    }
    recv_output[offset]='\0';
    dev_err(dev,"recv_dump:i=%d,len=%d,%s", i,len,recv_output);
}

void recv_dump_skb(struct device * dev,struct sk_buff *skb)
{
    int i;
    struct skb_shared_info *shinfo;
    dev_err(dev,"recv dump skb:skb->len=%d skb_headlen(skb)=%d",skb->len,skb_headlen(skb));
    recv_dump(dev,skb->data,skb_headlen(skb));
    shinfo = skb_shinfo(skb);
    for (i = 0; i < shinfo->nr_frags; i++) {
        recv_dump(dev,
             skb_frag_address(&shinfo->frags[i]),
             skb_frag_size(&shinfo->frags[i]));
    }
}

char send_output[4096];
void send_dump(struct device * dev,const char *buf, int len) {
    int i;
    int offset = 0;
    int ret;
    for (i = 0; i < len; i++) {
        ret = snprintf(send_output + offset,  sizeof(send_output) - offset, "0x%02X,", buf[i]);
        if (ret <= 0) {
            dev_err(dev,"recv overflow  %d,%d,%d", i, len,offset);
            break;
        }
        offset += ret;
    }
    send_output[offset]='\0';
    dev_err(dev,"send_dump:i=%d,len=%d,%s", i,len,send_output);
}

void send_dump_skb(struct device * dev,struct sk_buff *skb)
{
    int i;
    struct skb_shared_info *shinfo;
    dev_err(dev,"send dump skb:skb->len=%d skb_headlen(skb)=%d",skb->len,skb_headlen(skb));
    send_dump(dev,skb->data,skb_headlen(skb));
    shinfo = skb_shinfo(skb);
    for (i = 0; i < shinfo->nr_frags; i++) {
        send_dump(dev,
             skb_frag_address(&shinfo->frags[i]),
             skb_frag_size(&shinfo->frags[i]));
    }
}