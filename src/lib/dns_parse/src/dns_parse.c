#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <net/if.h>
#include <pcap.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <linux/if_packet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

#include "log.h"
#include "const.h"
#include "network.h"
#include "rtypes.h"
#include "strutils.h"
#include "policy_tags.h"
#include "os_util.h"
#include "memutil.h"

#include "fsm.h"
#include "fsm_policy.h"
#include "dns_cache.h"
#include "dns_parse.h"
#include "ds_tree.h"
#include "json_mqtt.h"
#include "ovsdb_utils.h"
#include "ovsdb_sync.h"
#include "wc_telemetry.h"

#define IP2ACTION_MIN_TTL (6*60*60)

void handler(uint8_t *, const struct pcap_pkthdr *, const uint8_t *);
void dns_rr_free(dns_rr *);
void dns_question_free(dns_question *);
uint32_t parse_rr(uint32_t, uint32_t, struct pcap_pkthdr *,
                  uint8_t *, dns_rr *);
void print_rr_section(dns_rr *, char *, struct dns_session *);
void print_packet(uint32_t, uint8_t *, uint32_t, uint32_t, u_int);

static struct dns_cache cache_mgr =
{
    .initialized = false,
};

struct dns_cache *dns_get_mgr(void)
{
    return &cache_mgr;
}

/**
 * @brief compare DNS messages
 * @param id1 dns message
 * @param id2 dns message
 *
 * Compare DNS messages based on their embedded IDs
 */
static int
dns_req_id_cmp(void *a, void *b)
{
    uint16_t *id_a = a;
    uint16_t *id_b = b;

    return ((int)(*id_a) - (int)(*id_b));
};

/**
 * @brief compare device sessions
 * @param dev_session_a: device session
 * @param dev_session_b: device session
 *
 * Compare device sessions based on their device IDs (MAC address)
 */
static int
dns_dev_id_cmp(void *a, void *b)
{
    os_macaddr_t *dev_id_a = a;
    os_macaddr_t *dev_id_b = b;

    return memcmp(dev_id_a->addr,
                  dev_id_b->addr,
                  sizeof(dev_id_a->addr));
}


static void
dns_parse_update(struct fsm_session *session)
{
    struct dns_session *dns_session = session->handler_ctxt;
    char *dbg_str = session->ops.get_config(session, "debug");
    char *cache_ip_str = session->ops.get_config(session, "cache_ip");
    char *mqtt_blocker_topic = session->ops.get_config(session, "blk_mqtt");
    char *hs_report_interval;
    char *hs_report_topic;
    long interval;
    int val;

    dns_session->health_stats_report_interval = (long)INT_MAX;
    hs_report_interval = session->ops.get_config(session,
                                                 "wc_health_stats_interval_secs");
    if (hs_report_interval != NULL)
    {
        interval = strtoul(hs_report_interval,  NULL, 10);
        dns_session->health_stats_report_interval = (long)interval;
    }
    hs_report_topic = session->ops.get_config(session,
                                              "wc_health_stats_topic");
    dns_session->health_stats_report_topic = hs_report_topic;

    if (dbg_str != NULL)
    {
        LOGT("%s: session %p: debug key value: %s",
             __func__, session, dbg_str);
        val = strcmp(dbg_str, "on");
        if (val == 0)
        {
            dns_session->debug = true;
        }
        else
        {
            val = strcmp(dbg_str, "off");
            if (val == 0)
            {
                dns_session->debug = false;
            }
        }
    }
    else
    {
        LOGT("%s: session %p could not find debug key in other_config",
             __func__, session);
        dns_session->debug = false;
    }

    LOGI("%s: debug %s", __func__,
         dns_session->debug == true ? "on" : "off");

    if (cache_ip_str != NULL)
    {
        LOGT("%s: session %p: cache_ip key value: %s",
             __func__, session, cache_ip_str);
        val = strcmp(cache_ip_str, "on");
        if (val == 0)
        {
            dns_session->cache_ip = true;
        } else
        {
            val = strcmp(cache_ip_str, "off");
            if (val == 0)
            {
                dns_session->cache_ip = false;
            }
        }
    }
    else
    {
        LOGT("%s: session %p could not find cache_ip key in other_config",
             __func__, session);
        dns_session->cache_ip = false;
    }
    LOGI("%s: cache_ip %s", __func__,
         dns_session->cache_ip == true ? "on" : "off");

    if (mqtt_blocker_topic != NULL)
    {
        dns_session->blocker_topic = mqtt_blocker_topic;
        LOGT("%s: session %p: mqtt_blocker_topic key value: %s",
             __func__, session, mqtt_blocker_topic);
    }
    else
    {
        LOGT("%s: session %p could not find blocker topic in other_config",
             __func__, session);
        dns_session->blocker_topic = NULL;
    }
    LOGI("%s: mqtt_blocker_topic %s", __func__,
         mqtt_blocker_topic ? mqtt_blocker_topic : "None");
}


/**
 * @brief compare sessions
 *
 * @param a session pointer
 * @param b session pointer
 * @return 0 if sessions matches
 */
static int
dns_session_cmp(void *a, void *b)
{
    uintptr_t p_a = (uintptr_t)a;
    uintptr_t p_b = (uintptr_t)b;

    if (p_a ==  p_b) return 0;
    if (p_a < p_b) return -1;
    return 1;
}


/**
 * @brief look up a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct dns_session *
dns_lookup_session(struct fsm_session *session)
{
    struct dns_cache *mgr;
    struct dns_session *d_session;
    ds_tree_t *sessions;

    mgr = dns_get_mgr();
    sessions = &mgr->fsm_sessions;

    d_session = ds_tree_find(sessions, session);
    return d_session;
}

/**
 * @brief get a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct dns_session *
dns_get_session(struct fsm_session *session)
{
    struct dns_cache *mgr;
    struct dns_session *d_session;
    ds_tree_t *sessions;

    mgr = dns_get_mgr();
    sessions = &mgr->fsm_sessions;

    d_session = ds_tree_find(sessions, session);
    if (d_session != NULL) return d_session;

    LOGD("%s: Adding new session %s", __func__, session->name);
    d_session = CALLOC(1, sizeof(struct dns_session));
    if (d_session == NULL) return NULL;

    d_session->initialized = false;
    ds_tree_insert(sessions, d_session, session);

    return d_session;
}

void
dns_free_device(struct dns_device *ddev)
{
    FREE(ddev);
}


/**
 * @brief Frees a dns session
 *
 * @param d_session the dns session to delete
 */
void
dns_free_session(struct dns_session *d_session)
{
    struct dns_device *ddev, *remove;
    ds_tree_t *tree;

    tree = &d_session->session_devices;
    ddev = ds_tree_head(tree);
    while (ddev != NULL)
    {
        remove = ddev;
        ddev = ds_tree_next(tree, ddev);
        ds_tree_remove(tree, remove);
        dns_free_device(remove);
    }
    FREE(d_session);
}


/**
 * @brief deletes a session
 *
 * @param session the fsm session keying the dns session to delete
 */
void
dns_delete_session(struct fsm_session *session)
{
    struct dns_cache *mgr;
    struct dns_session *d_session;
    ds_tree_t *sessions;

    mgr = dns_get_mgr();
    sessions = &mgr->fsm_sessions;

    d_session = ds_tree_find(sessions, session);
    if (d_session == NULL) return;

    LOGD("%s: removing session %s", __func__, session->name);
    ds_tree_remove(sessions, d_session);
    dns_free_session(d_session);

    return;
}


void
dns_plugin_exit(struct fsm_session *session)
{
    struct dns_cache *mgr;

    mgr = dns_get_mgr();
    if (!mgr->initialized) return;

    dns_cache_cleanup_mgr();
    dns_delete_session(session);
}


void
dns_set_provider(struct fsm_session *session)
{
    ds_tree_t *other_config;
    struct str_pair *pair;
    char *provider;

    other_config = session->conf->other_config;
    if (other_config == NULL) return;

    provider = session->ops.get_config(session, "provider_plugin");
    if (provider != NULL) return;

    provider = session->ops.get_config(session, "provider");
    if (provider != NULL) return;

    pair = get_pair("provider", "brightcloud");
    if (pair == NULL) return;

    ds_tree_insert(other_config, pair, pair->key);
    session->p_ops->parser_ops.get_service(session);
}


int
dns_set_forward_context(struct fsm_session *session)
{
    struct dns_session *dns_session;
    struct ifreq ifreq_c;
    struct ifreq ifr_i;

    dns_session = dns_get_session(session);

    /* Open raw socket to re-inject DNS responses */
    dns_session->sock_fd = socket(AF_PACKET, SOCK_RAW, 0);
    if (dns_session->sock_fd < 0)
    {
        LOGE("%s: socket() failed (%s)",
             __func__, strerror(errno));
        return -1;
    }

    memset(&ifr_i, 0, sizeof(ifr_i));
    STRSCPY(ifr_i.ifr_name, session->tx_intf);

    if ((ioctl(dns_session->sock_fd, SIOCGIFINDEX, &ifr_i)) < 0)
    {
        LOGE("%s: error in index ioctl reading (%s)",
             __func__, strerror(errno));
        return -1;
    }

    dns_session->raw_dst.sll_family = PF_PACKET;
    dns_session->raw_dst.sll_ifindex = ifr_i.ifr_ifindex;
    dns_session->raw_dst.sll_halen = ETH_ALEN;

    memset(&ifreq_c, 0, sizeof(ifreq_c));
    STRSCPY(ifreq_c.ifr_name, session->tx_intf);

    if ((ioctl(dns_session->sock_fd, SIOCGIFHWADDR, &ifreq_c)) < 0)
    {
        LOGE("%s: error in SIOCGIFHWADDR ioctl reading (%s)",
             __func__, strerror(errno));
        return -1;
    }

    memcpy(dns_session->src_eth_addr.addr, ifreq_c.ifr_hwaddr.sa_data, 6);

    return 0;
}


void
dns_mgr_init(void)
{
    struct dns_cache *mgr;

    mgr = dns_get_mgr();
    if (mgr->initialized) return;

    ds_tree_init(&mgr->fsm_sessions, dns_session_cmp,
                 struct dns_session, session_node);
    mgr->set_forward_context = dns_set_forward_context;
    mgr->forward = dns_forward;
    mgr->update_tag = dns_update_tag;
    mgr->policy_init = fsm_policy_init;
    mgr->policy_check = fqdn_policy_check;
    mgr->req_cache_ttl = REQ_CACHE_TTL;

    /* Initialize the DNS cache */
    dns_cache_init();

    mgr->initialized = true;
}


int
dns_plugin_init(struct fsm_session *session)
{
    struct dns_cache *mgr;
    struct dns_session *dns_session;
    struct fsm_parser_ops *parser_ops;
    time_t now;
    int rc;

    mgr = dns_get_mgr();

    /* Initialize the manager on first call */
    dns_mgr_init();

    /* Look up the dns session */
    dns_session = dns_get_session(session);
    if (dns_session == NULL)
    {
        LOGE("%s: could not allocate dns parser", __func__);
        return -1;
    }

    /* Bail if the session is already initialized */
    if (dns_session->initialized) return 0;

    session->ops.update = dns_parse_update;
    session->ops.periodic = dns_periodic;
    session->handler_ctxt = dns_session;
    session->ops.exit = dns_plugin_exit;

    /* Set the plugin specific ops */
    parser_ops = &session->p_ops->parser_ops;
    parser_ops->handler = dns_handler;

    /* Setting configuration defaults. */
    dns_session->RECORD_SEP = "";
    dns_session->SEP = '\t';

    dns_session->ip_config.ip_fragment_head = NULL;
    dns_session->fsm_context = session;

    rc = mgr->set_forward_context(session);
    if (rc != 0) goto error;

    now = time(NULL);
    dns_session->stat_report_ts = now;
    dns_session->debug = false;
    dns_set_provider(session);
    mgr->policy_init();

    ds_tree_init(&dns_session->session_devices, dns_dev_id_cmp,
                 struct dns_device, device_node);

    dns_session->cat_offline.check_offline = 30;
    dns_session->cat_offline.provider_offline = false;
    dns_session->initialized = true;
    return 0;

error:
    dns_delete_session(session);
    return -1;
}


static void
dns_prepare_forward(uint8_t *packet,
                    struct dns_session *dns_session)
{
    ip_info *ip = &dns_session->ip;
    ip_addr *src = &ip->src;
    ip_addr *dst = &ip->dst;
    char buf[128] = { 0 };

    LOGD("%s: source mac: " PRI(os_macaddr_t) ", dst mac: " PRI(os_macaddr_t),
        __func__,
        FMT(os_macaddr_t, dns_session->eth_hdr.srcmac),
        FMT(os_macaddr_t, dns_session->eth_hdr.dstmac));

    if (ip->src.vers == IPv4)
    {
        /* UDP checksum to 0 */
        memset(dns_session->udp.udp_csum_ptr, 0, 2);

        inet_ntop(AF_INET, &dst->addr.v4.s_addr, buf, sizeof(buf));
        LOGD("%s: dst address %s, dst port %d, checksum 0x%x",
             __func__,
             buf, dns_session->udp.dstport, dns_session->udp.udp_checksum);

        inet_ntop(AF_INET, &src->addr.v4.s_addr, buf, sizeof(buf));
        LOGD("%s: src address %s, src port %d",
             __func__, buf, dns_session->udp.srcport);

    }
    else
    {   /* IPv6 */
        uint16_t csum = compute_udp_checksum(packet, ip, &dns_session->udp);

        *(uint16_t *)dns_session->udp.udp_csum_ptr = csum;
    }

    memcpy(dns_session->raw_dst.sll_addr, dns_session->eth_hdr.dstmac.addr,
           sizeof(dns_session->eth_hdr.dstmac.addr));
    memcpy(packet + 6, dns_session->src_eth_addr.addr,
           sizeof(dns_session->src_eth_addr.addr));

}


static void
process_response_ip(struct fqdn_pending_req *req,
                    char *ip, int len)
{
    if (len == INET_ADDRSTRLEN)
    {
        if (req->dns_response.ipv4_cnt == MAX_RESOLVED_ADDRS) return;

        req->dns_response.ipv4_addrs[req->dns_response.ipv4_cnt] = STRDUP(ip);
        req->dns_response.ipv4_cnt++;
    }

    if (len == INET6_ADDRSTRLEN)
    {
        if (req->dns_response.ipv6_cnt == MAX_RESOLVED_ADDRS) return;

        req->dns_response.ipv6_addrs[req->dns_response.ipv6_cnt] = STRDUP(ip);
        req->dns_response.ipv6_cnt++;
    }
}


static void
dns_parse_populate_sockaddr(int af, void *ip_addr, struct sockaddr_storage *dst)
{
    if (!ip_addr) return;

    if (af == AF_INET)
    {
        struct sockaddr_in *in4 = (struct sockaddr_in *)dst;

        memset(in4, 0, sizeof(struct sockaddr_in));
        in4->sin_family = af;
        memcpy(&in4->sin_addr, ip_addr, sizeof(in4->sin_addr));
    }
    else if (af == AF_INET6)
    {
        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)dst;

        memset(in6, 0, sizeof(struct sockaddr_in6));
        in6->sin6_family = af;
        memcpy(&in6->sin6_addr, ip_addr, sizeof(in6->sin6_addr));
    }
    return;
}

/**
 * @brief Set wb details..
 *
 * receive wb dst and src.
 *
 * @return void.
 *
 */
void
populate_dns_wb_cache_entry(struct ip2action_wb_info *i2a_cache_wb,
                            struct fsm_wp_info *fqdn_reply_wb)
{
    i2a_cache_wb->risk_level = fqdn_reply_wb->risk_level;
}

/**
 * @brief Set bc details.
 *
 * receive bc dst, src and nelems.
 *
 * @return void.
 *
 */
void
populate_dns_bc_cache_entry(struct ip2action_bc_info *i2a_cache_bc,
                            struct fsm_bc_info *fqdn_reply_bc,
                            uint8_t nelems)
{
    size_t index;

    i2a_cache_bc->reputation = fqdn_reply_bc->reputation;
    for (index = 0; index < nelems; index++)
    {
        i2a_cache_bc->confidence_levels[index] =
            fqdn_reply_bc->confidence_levels[index];
    }
}

/**
 * @brief Set gk details.
 *
 * receive gk dst and src.
 *
 * @return void.
 *
 */
void
populate_dns_gk_cache_entry(struct ip2action_gk_info *i2a_cache_gk,
                            struct fsm_gk_info *fqdn_reply_gk)
{
    i2a_cache_gk->confidence_level = fqdn_reply_gk->confidence_level;
    i2a_cache_gk->category_id = fqdn_reply_gk->category_id;
    if (fqdn_reply_gk->gk_policy)
    {
        i2a_cache_gk->gk_policy = fqdn_reply_gk->gk_policy;
    }
}


static void
process_response_ips(dns_info *dns, uint8_t *packet,
                     struct fqdn_pending_req *req,
                     struct fsm_policy_reply *policy_reply)
{
    struct ip2action_req ip_cache_req;
    struct sockaddr_storage ipaddr;
    int ip2action_cache_ttl;
    dns_rr *answer;
    const char *res;
    int qtype = -1;
    size_t index;
    uint32_t ttl;
    int i = 0;
    void *ip;
    bool rc;
    bool cache;
    bool add_entry;

    if (dns == NULL) return;
    if (req->dns_response.num_replies > 1) return;

    if (dns->queries == NULL) LOGT("%s: no queries", __func__);

    ttl = 0;
    answer = dns->answers;
    qtype = dns->queries->type;
    LOGT("%s: query type: %d",
         __func__, qtype);

    for (i = 0; i < dns->ancount && answer != NULL; i++)
    {
        LOGT("%s: answer %d type: %d",
             __func__, i, qtype);
        if (answer->type == qtype)
        {
            add_entry = false;
            ttl = answer->ttl;
            ip = packet + answer->type_pos + 10;
            LOGT("%s: type %d answer, addr %s ttl: %d",
                 __func__, qtype, answer->data, ttl);
            if (qtype == 1) /* IPv4 redirect */
            {
                char ipv4_addr[INET_ADDRSTRLEN];

                res = inet_ntop(AF_INET, packet + answer->type_pos + 10,
                                ipv4_addr, INET_ADDRSTRLEN);
                if (res == NULL)
                {
                    LOGE("%s: inet_ntop failed: %s", __func__,
                         strerror(errno));
                }
                else
                {
                    add_entry = true;
                    dns_parse_populate_sockaddr(AF_INET, ip, &ipaddr);
                    process_response_ip(req, ipv4_addr, INET_ADDRSTRLEN);
                }
            }
            else if (qtype == 28) /* IPv6 */
            {
                char ipv6_addr[INET6_ADDRSTRLEN];

                res = inet_ntop(AF_INET6, packet + answer->type_pos + 10,
                                ipv6_addr, INET6_ADDRSTRLEN);
                if (res == NULL)
                {
                    LOGE("%s: inet_ntop failed: %s", __func__,
                         strerror(errno));
                }
                else
                {
                    add_entry = true;
                    dns_parse_populate_sockaddr(AF_INET6, ip, &ipaddr);
                    process_response_ip(req, ipv6_addr, INET6_ADDRSTRLEN);
                }
            }

            cache = (add_entry && (req->req_info->reply != NULL) &&
                     (!req->req_info->reply->connection_error) &&
                     (policy_reply->categorized == FSM_FQDN_CAT_SUCCESS));

            if (cache)
            {
                memset(&ip_cache_req, 0, sizeof(ip_cache_req));
                ip_cache_req.device_mac = &req->dev_id;
                ip_cache_req.ip_addr = &ipaddr;
                ip2action_cache_ttl = ((ttl < IP2ACTION_MIN_TTL) ?
                                       IP2ACTION_MIN_TTL : (int)ttl);
                ip_cache_req.cache_ttl = ((req->rd_ttl != -1) ?
                                          req->rd_ttl : ip2action_cache_ttl);
                ip_cache_req.action = policy_reply->action;
                ip_cache_req.policy_idx = policy_reply->policy_idx;
                ip_cache_req.service_id = req->req_info->reply->service_id;
                ip_cache_req.nelems = req->req_info->reply->nelems;
                ip_cache_req.cat_unknown_to_service = policy_reply->cat_unknown_to_service;
                for (index = 0; index < req->req_info->reply->nelems; ++index)
                {
                    ip_cache_req.categories[index] =
                        req->req_info->reply->categories[index];
                }

                if (ip_cache_req.service_id == IP2ACTION_BC_SVC)
                {
                    populate_dns_bc_cache_entry(&ip_cache_req.cache_bc,
                                                &req->req_info->reply->bc,
                                                req->req_info->reply->nelems);
                }
                else if (ip_cache_req.service_id == IP2ACTION_WP_SVC)
                {
                    populate_dns_wb_cache_entry(&ip_cache_req.cache_wb,
                                                &req->req_info->reply->wb);
                }
                else if (ip_cache_req.service_id == IP2ACTION_GK_SVC)
                {
                    populate_dns_gk_cache_entry(&ip_cache_req.cache_gk,
                                                &req->req_info->reply->gk);
                }
                else
                {
                    LOGD("%s : service id %d no recognized", __func__,
                         req->req_info->reply->service_id);
                }

                rc = dns_cache_add_entry(&ip_cache_req);
                if (rc == false)
                {
                    LOGW("%s: Couldn't add ip2action entry to cache.", __func__);
                }
            }
        }
        answer = answer->next;
    }
}


void
dns_forward(struct dns_session *dns_session, dns_info *dns,
            uint8_t *packet, int len)
{
    int rc = 0;

    dns_prepare_forward(packet, dns_session);
    rc = sendto(dns_session->sock_fd, packet, len, 0,
                (struct sockaddr *)&dns_session->raw_dst,
                sizeof(dns_session->raw_dst));
    if (rc < 0)
    {
        LOGE("Could not forward DNS response (%s)", strerror(errno));
    }
}


static bool
dns_updatev4_tag(struct fqdn_pending_req *req, struct fsm_policy_reply *policy_reply)
{
    struct schema_Openflow_Local_Tag *local_tag;
    struct schema_Openflow_Tag *regular_tag;
    size_t max_capacity;
    size_t value_len;
    size_t elem_len;
    int tle_flag;
    bool result;
    size_t len;

    regular_tag = NULL;
    local_tag = NULL;
    max_capacity = 0;
    result = false;
    len = 0;

    if (policy_reply->action != FSM_UPDATE_TAG) return false;

    tle_flag = om_get_type_of_tag(policy_reply->updatev4_tag);

    if (tle_flag == OM_TLE_FLAG_DEVICE ||
        tle_flag == OM_TLE_FLAG_CLOUD)
    {
        regular_tag = CALLOC(1, sizeof(*regular_tag));
        if (regular_tag == NULL) goto out;

        value_len = sizeof(regular_tag->device_value);
        elem_len = sizeof(regular_tag->device_value[0]);
        max_capacity = value_len / elem_len;

        len = strlen(policy_reply->updatev4_tag);
        os_util_strncpy(regular_tag->name, &policy_reply->updatev4_tag[3], len - 3);
        regular_tag->name_exists = true;
        regular_tag->name_present = true;

        result = dns_generate_update_tag(req, policy_reply, regular_tag->device_value,
                                         &regular_tag->device_value_len,
                                         max_capacity, 4);
        if (result)
        {
            result = dns_upsert_regular_tag(regular_tag, ovsdb_sync_upsert);
            if (!result)
            {
                LOGT("%s: Openflow_Tag not updated for request.", __func__);
            }
        }
        else
        {
            LOGT("%s: Openflow_Tag not updated for request.", __func__);
        }
    }
    else if (tle_flag == OM_TLE_FLAG_LOCAL)
    {
        local_tag = CALLOC(1, sizeof(*local_tag));
        if (local_tag == NULL) goto out;

        value_len = sizeof(local_tag->values);
        elem_len = sizeof(regular_tag->device_value[0]);
        max_capacity = value_len / elem_len;

        len = strlen(policy_reply->updatev4_tag);
        os_util_strncpy(local_tag->name, &policy_reply->updatev4_tag[3], len - 3);
        local_tag->name_exists = true;
        local_tag->name_present = true;

        result = dns_generate_update_tag(req, policy_reply, local_tag->values,
                                         &local_tag->values_len,
                                         max_capacity, 4);
        if (result)
        {
            result = dns_upsert_local_tag(local_tag, ovsdb_sync_upsert);
            if (!result)
            {
                LOGT("%s: Openflow_Local_Tag not updated for request.", __func__);
            }
        }
        else
        {
            LOGT("%s: Openflow_Local_Tag not updated for request.", __func__);
        }
    }

out:
    FREE(regular_tag);
    FREE(local_tag);

    return result;
}


bool
dns_updatev6_tag(struct fqdn_pending_req *req, struct fsm_policy_reply *policy_reply)
{
    struct schema_Openflow_Local_Tag *local_tag;
    struct schema_Openflow_Tag *regular_tag;
    size_t max_capacity;
    size_t value_len;
    size_t elem_len;
    int tle_flag;
    bool result;
    size_t len;

    regular_tag = NULL;
    local_tag = NULL;
    max_capacity = 0;
    result = false;
    len = 0;

    if (policy_reply->action != FSM_UPDATE_TAG) return false;

    tle_flag = om_get_type_of_tag(policy_reply->updatev6_tag);

    if (tle_flag == OM_TLE_FLAG_DEVICE ||
        tle_flag == OM_TLE_FLAG_CLOUD)
    {
        regular_tag = CALLOC(1, sizeof(*regular_tag));
        if (regular_tag == NULL) goto out;

        value_len = sizeof(regular_tag->device_value);
        elem_len = sizeof(regular_tag->device_value[0]);
        max_capacity = value_len / elem_len;

        len = strlen(policy_reply->updatev6_tag);
        os_util_strncpy(regular_tag->name, &policy_reply->updatev6_tag[3], len - 3);
        regular_tag->name_exists = true;
        regular_tag->name_present = true;

        result = dns_generate_update_tag(req, policy_reply, regular_tag->device_value,
                                         &regular_tag->device_value_len,
                                         max_capacity, 6);
        if (result)
        {
            result = dns_upsert_regular_tag(regular_tag, ovsdb_sync_upsert);
            if (!result)
            {
                LOGT("%s: Openflow_Tag not updated for request.", __func__);
            }
        }
        else
        {
            LOGT("%s: Openflow_Tag not updated for request.", __func__);
        }
    }
    else if(tle_flag == OM_TLE_FLAG_LOCAL)
    {
        local_tag = CALLOC(1, sizeof(*local_tag));
        if (local_tag == NULL) goto out;

        value_len = sizeof(local_tag->values);
        elem_len = sizeof(regular_tag->device_value[0]);
        max_capacity = value_len / elem_len;

        len = strlen(policy_reply->updatev6_tag);
        os_util_strncpy(local_tag->name, &policy_reply->updatev6_tag[3], len - 3);
        local_tag->name_exists = true;
        local_tag->name_present = true;

        result = dns_generate_update_tag(req, policy_reply, local_tag->values,
                                         &local_tag->values_len,
                                         max_capacity, 6);
        if (result)
        {
            result = dns_upsert_local_tag(local_tag, ovsdb_sync_upsert);
            if (!result)
            {
                LOGT("%s: Openflow_Local_Tag not updated for request.", __func__);
            }
        }
        else
        {
            LOGT("%s: Openflow_Local_Tag not updated for request.", __func__);
        }
    }

out:
    FREE(regular_tag);
    FREE(local_tag);

    return result;
}


static bool
is_device_excluded(char *tag, os_macaddr_t *mac)
{
    char mac_s[32] = { 0 };

    snprintf(mac_s, sizeof(mac_s), PRI_os_macaddr_lower_t,
             FMT_os_macaddr_pt(mac));

    return om_tag_in(mac_s, tag);
}


void
dns_update_tag(struct fqdn_pending_req *req, struct fsm_policy_reply *policy_reply)
{
    bool rc = false;

    if (!req) return;

    rc = is_device_excluded(policy_reply->excluded_devices, &req->dev_id);
    if (rc)
    {
        LOGD("%s: mac " PRI_os_macaddr_lower_t " is excluded from tag updates."
              ,__func__, FMT_os_macaddr_t(req->dev_id));
        return;
    }

    if (policy_reply->updatev4_tag && req->dns_response.ipv4_cnt != 0)
    {
        rc = dns_updatev4_tag(req, policy_reply);
        if (!rc)
        {
            LOGT("%s: Failed to update ipv4 OpenFlow tags.", __func__);
        }
    }

    if (policy_reply->updatev6_tag &&
        req->dns_response.ipv6_cnt != 0)
    {
        rc = dns_updatev6_tag(req, policy_reply);
        if (!rc)
        {
            LOGT("%s: Failed to update ipv6 OpenFlow tags.",__func__);
        }
    }

    return;
}


static char redirect_prefix[RD_SZ][4] =
{
    "A-", "4A-", "C-"
};

static char *
check_redirect(char *redirect, int id)
{
    char *cmp = redirect_prefix[id];

    LOGT("%s: redirect: %s", __func__, redirect);
    if (strncmp(redirect, cmp, strlen(cmp)) == 0)
    {
        return redirect + strlen(cmp);
    }
    return NULL;
}

bool
dns_cache_add_redirect_entry(struct fqdn_pending_req *req,
                             struct sockaddr_storage *ipaddr)
{
    struct ip2action_req ip_cache_req;
    int rc;

    LOGD("%s(): adding redirect cache entry", __func__);

    memset(&ip_cache_req, 0, sizeof(ip_cache_req));

    if (req->req_info == NULL || req->req_info->reply == NULL) return false;

    ip_cache_req.device_mac = &req->dev_id;
    ip_cache_req.ip_addr = ipaddr;
    ip_cache_req.service_id = req->req_info->reply->service_id;
    ip_cache_req.action = FSM_ALLOW;
    ip_cache_req.redirect_flag = true;

    /* set required values for adding to cache */
    if (ip_cache_req.service_id == IP2ACTION_BC_SVC)
    {
        ip_cache_req.nelems = 1;
        ip_cache_req.cache_info.bc_info.reputation = 3;
        ip_cache_req.cache_info.bc_info.confidence_levels[0] = 1;
    }
    else if (ip_cache_req.service_id == IP2ACTION_WP_SVC)
    {
        ip_cache_req.cache_info.wb_info.risk_level = 5;
    }

    /* 96 hours TTL */
    ip_cache_req.cache_ttl = DNS_REDIRECT_TTL;

    rc = dns_cache_add_entry(&ip_cache_req);
    return rc;
}

static bool
update_a_rrs(dns_info *dns, uint8_t *packet,
             struct fqdn_pending_req *req,
             struct fsm_policy_reply *policy_reply)
{
    dns_rr *answer = dns->answers;
    struct sockaddr_storage ipaddr;
    bool updated = false;
    int qtype = -1;
    int i = 0;
    void *ip;
    bool rc;

    if (dns->queries == NULL)
    {
        LOGT("%s: no queries", __func__);
        return false;
    }

    if (policy_reply->redirect == false) return false;

    qtype = dns->queries->type;
    LOGT("%s: query type: %d",
         __func__, qtype);
    for (i = 0; i < dns->ancount && answer != NULL; i++)
    {
        LOGT("%s: answer %d type: %d",
             __func__, i, qtype);
        if (answer->type == qtype)
        {
            uint8_t *p_ttl = packet + answer->type_pos + 4;

            LOGT("%s: type %d answer, addr %s",
                 __func__, qtype, answer->data);
            if (qtype == 1)  /* IPv4 redirect */
            {
                char *ipv4_addr = check_redirect(policy_reply->redirects[0],
                                                 IPv4_REDIRECT);
                if (ipv4_addr == NULL)
                {
                    ipv4_addr = check_redirect(policy_reply->redirects[1],
                                               IPv4_REDIRECT);
                }
                if (ipv4_addr != NULL)
                {
                    inet_pton(AF_INET, ipv4_addr,
                              packet + answer->type_pos + 10);
                    ip = packet + answer->type_pos + 10;
                    if (req->rd_ttl != -1)
                    {
                        *(uint32_t *)(p_ttl) = htonl(req->rd_ttl);
                    }
                    /* Add the redirected IP to cache */
                    LOGT("%s(): adding redirected IP %s to cache", __func__, ipv4_addr);
                    dns_parse_populate_sockaddr(AF_INET, ip, &ipaddr);
                    rc = dns_cache_add_redirect_entry(req, &ipaddr);
                    if (rc == false)
                    {
                        LOGD("%s(): failed to add %s to cache", __func__, ipv4_addr);
                    }
                    updated |= true;
                }
            }
            else if (qtype == 28)  /* IPv6 */
            {
                LOGT("%s: IPv6 record, rdlength == %d",
                     __func__, answer->rdlength);
                char *ipv6_addr = check_redirect(policy_reply->redirects[0],
                                                 IPv6_REDIRECT);
                if (ipv6_addr == NULL)
                {
                    ipv6_addr = check_redirect(policy_reply->redirects[1],
                                               IPv6_REDIRECT);
                }
                if (ipv6_addr != NULL)
                {
                    inet_pton(AF_INET6, ipv6_addr,
                              packet + answer->type_pos + 10);
                    ip = packet + answer->type_pos + 10;
                    if (req->rd_ttl != -1)
                    {
                        *(uint32_t *)(p_ttl) = htonl(req->rd_ttl);
                    }
                    updated |= true;

                    LOGT("%s(): adding redirected IPv6 %s to cache", __func__, ipv6_addr);
                    dns_parse_populate_sockaddr(AF_INET6, ip, &ipaddr);
                    rc = dns_cache_add_redirect_entry(req, &ipaddr);

                    if (rc == false)
                    {
                        LOGD("%s(): failed to add IPv6 addr %s to cache", __func__, ipv6_addr);
                    }
                }
            }
        }
        answer = answer->next;
    }
    return updated;
}


static void
dns_handle_reply(struct dns_session *dns_session, dns_info *dns,
                 eth_info *eth, struct pcap_pkthdr *header,
                 uint8_t *packet)
{
    struct dns_device *ds = NULL;
    struct fqdn_pending_req *req = NULL;
    struct fsm_session *session;
    struct fsm_policy_reply *policy_reply;
    struct dns_cache *mgr;

    mgr = dns_get_mgr();

    LOGD("dns reply: looking up device " PRI(os_macaddr_lower_t),
         FMT(os_macaddr_t, eth->dstmac));

    session = dns_session->fsm_context;
    ds = ds_tree_find(&dns_session->session_devices, &eth->dstmac);
    if (ds == NULL)
    {
        LOGD("dns reply: could not find device " PRI(os_macaddr_lower_t),
             FMT(os_macaddr_t, eth->dstmac));
        goto free_out;
    }

    LOGD("dns reply: looking up request %u type %d",
         dns->id, dns->answers ? dns->answers->type : -1);
    req = ds_tree_find(&ds->fqdn_pending_reqs, &dns->id);
    if (req == NULL)
    {
        LOGD("dns reply: could not retrieve request %u type %d",
             dns->id, dns->answers ? dns->answers->type : -1);
        goto free_out;
    }

    policy_reply = ds_tree_find(&ds->dns_policy_replies_tree, &dns->id);
    if (policy_reply == NULL)
    {
        LOGD("%s(): could not retrieve policy response for request %u",
             __func__, dns->id);
        goto free_out;
    }

    req->dns_response.num_replies++;
    dns_session->req = req;
    process_response_ips(dns, packet, req, policy_reply);

    if (policy_reply->action == FSM_UPDATE_TAG)
    {
       /*
        * Update Tag if we are interested
        * in the IPs returned.
        */
        mgr->update_tag(req, policy_reply);
    }

    /* forward the DNS response if the session has no categorization provider */
    if (session->provider_ops == NULL)
    {
        LOGT("%s: %s session : no provider available", __func__,
             session->name);
        mgr->forward(dns_session, dns, packet, header->caplen);
        dns_remove_req(dns_session, &eth->dstmac, req->req_id);
        goto free_out;
    }

    /* forward DNS response to category filter */
    if (session->provider_ops->dns_response)
    {
        LOGT("%s: dns reply: forwarding to category provider %s", __func__,
             session->provider);
        session->provider_ops->dns_response(session, req);
    }

    if (policy_reply->action == FSM_FORWARD)
    {
        mgr->forward(dns_session, dns, packet, header->caplen);
        dns_remove_req(dns_session, &eth->dstmac, req->req_id);
    }
    else if (dns->ancount == 0)
    {
        /*
         * If the DNS server did not provide a meaningful answer,
         * forward the reply
         */
        mgr->forward(dns_session, dns, packet, header->caplen);
        dns_remove_req(dns_session, &eth->dstmac, req->req_id);
    }
    else if (policy_reply->action == FSM_BLOCK)
    {
        char reason[128] = { 0 };
        char risk[128] = { 0 };

        if (policy_reply->categorized == FSM_FQDN_CAT_SUCCESS) {
            if (session->provider_ops->cat2str && (policy_reply->cat_match != -1))
            {
                snprintf(reason, sizeof(reason), "categorized as %s",
                         session->provider_ops->cat2str(session,
                                                        policy_reply->cat_match));
            }
            if (policy_reply->risk_level != -1)
            {
                snprintf(risk, sizeof(risk), "risk level %d",
                         policy_reply->risk_level);
            }
        }
        else if (policy_reply->categorized == FSM_FQDN_CAT_NOP)
        {
            snprintf(reason, sizeof(reason), "(blacklisted)");
        }
        else
        {
            snprintf(reason, sizeof(reason), "for unclear reasons");
        }
        LOGI("device " PRI(os_macaddr_lower_t)
             ": blocking access to %s %s %s",
             FMT(os_macaddr_t, eth->dstmac),
             req->req_info->url,
             reason, risk);
        if (update_a_rrs(dns, packet, req, policy_reply) == true)
        {
            mgr->forward(dns_session, dns, packet, header->caplen);
        }
        dns_remove_req(dns_session, &eth->dstmac, req->req_id);
    }
    else if (policy_reply->redirect == true)
    {
        update_a_rrs(dns, packet, req, policy_reply);
        mgr->forward(dns_session, dns, packet, header->caplen);
        dns_remove_req(dns_session, &eth->dstmac, req->req_id);
    }
    else if (policy_reply->fsm_checked == true)
    {
        mgr->forward(dns_session, dns, packet, header->caplen);
        dns_remove_req(dns_session, &eth->dstmac, req->req_id);
    }
    else
    {
        LOGD("%s: stashing dns reply %u", __func__, req->req_id);
        req->dns_reply_pkt = CALLOC(1, header->caplen);
        if (req->dns_reply_pkt == NULL)
        {
            LOGE("Could not allocate memory for dns response %d",
                 req->req_id);
            dns_remove_req(dns_session, &eth->dstmac, req->req_id);
        }
        else
        {
            memcpy(req->dns_reply_pkt, packet, header->caplen);
            req->dns_reply_pkt_len = header->caplen;
        }
    }

  free_out:
    free_rrs(&dns_session->ip, &dns_session->udp, dns, header);
    return;
}


/**
 * @brief set web categorization provider ops to the DNS request
 *
 * Sets the request's web categorization operations. If not set,
 * the policy engine will pass categorization calls.
 * operations are not set if:
 * - no web categorization backend was provided
 * - the categorization is offline
 * @param dns_session the session container
 * @param policy_reply the reply received from the provider
 */
void
set_provider_ops(struct dns_session *dns_session,
                 struct fsm_policy_reply *policy_reply)
{
    struct web_cat_offline *offline;
    struct fsm_session *session;

    session = dns_session->fsm_context;

    /* No backend available. Bail */
    if (session->provider_ops == NULL) return;

    /* Check if the backend provider is offline */
    offline = &dns_session->cat_offline;
    if (offline->provider_offline)
    {
        time_t now = time(NULL);
        bool backoff;

        backoff = ((now - offline->offline_ts) < offline->check_offline);

        /* Within the back off interval. Bail */
        if (backoff) return;

        /* Out of the back off interval. Reset offline marker */
        offline->provider_offline = false;
    }

    /* Set the backend provider ops */
    policy_reply->categories_check = session->provider_ops->categories_check;
    policy_reply->risk_level_check = session->provider_ops->risk_level_check;
    policy_reply->gatekeeper_req = session->provider_ops->gatekeeper_req;
}


void
dns_handler(struct fsm_session *session, struct net_header_parser *net_header)
{
    struct dns_session *dns_session;
    eth_info *eth;
    dns_question *qnext;
    os_macaddr_t *mac = NULL;
    struct fsm_url_request *req_info = NULL;
    int cnt = 0;
    int pos, i;
    dns_info dns = { 0 };
    struct dns_device *ds = NULL;
    struct fqdn_pending_req *req = NULL;
    struct fsm_policy_reply *policy_reply;
    struct pcap_pkthdr header;
    uint8_t * packet;

    dns_session = (struct dns_session *)session->handler_ctxt;
    eth = &dns_session->eth_hdr;

    /*
     * The way we handle IP fragments means we may have to replace
     * the original data and correct the header info, so a const won't work.
     */
    packet = (uint8_t *)net_header->start;
    header.caplen = net_header->caplen;
    header.len = net_header->caplen;

    if (dns_session->debug == true)
    {
        size_t len = (sizeof(dns_session->debug_pkt_copy) > header.len ?
                      header.len : sizeof(dns_session->debug_pkt_copy));
        memcpy(dns_session->debug_pkt_copy, packet, len);
        dns_session->debug_pkt_len = len;
    }

    pos = eth_parse(&header, packet, eth, &dns_session->eth_config);
    if (pos == 0) return;

    dns_session->post_eth = pos;

    if (eth->ethtype == 0x0800)
    {
        pos = ipv4_parse(pos, &header, &packet, &dns_session->ip,
                         &dns_session->ip_config);
    }
    else if (eth->ethtype == 0x86DD)
    {
        pos = ipv6_parse(pos, &header, &packet, &dns_session->ip,
                         &dns_session->ip_config);
    }
    else
    {
        LOGD("%s: Unsupported EtherType: %04x\n", __func__, eth->ethtype);
        return;
    }

    if (packet == NULL) return;

    if (dns_session->ip.proto != 17) return;

    pos = udp_parse(pos, &header, packet, &dns_session->udp);
    if (pos == 0) return;

    dns_session->data_offset = pos;
    pos = dns_parse(pos, &header, packet, &dns, dns_session, !FORCE);
    if (dns.qdcount == 0)
    {
        LOGD("%s: dropping packet with no question", __func__);
        free_rrs(&dns_session->ip, &dns_session->udp, &dns, &header);
        return;
    }

    dns_session->last_byte_pos = pos;
    if (dns.qr == 1)
    {
        dns_handle_reply(dns_session, &dns, eth, &header, packet);
        return;
    }

    qnext = dns.queries;
    mac = (dns.qr == 0 ? &eth->srcmac : &eth->dstmac);

    LOGD("%s: looking up device " PRI_os_macaddr_lower_t,
         __func__, FMT_os_macaddr_pt(mac));
    ds = ds_tree_find(&dns_session->session_devices, mac);

    /* Look for the device in the device tree */
    if (ds == NULL)
    {
        ds = CALLOC(sizeof(*ds), 1);
        memcpy(ds->device_mac.addr, eth->srcmac.addr,
               sizeof(ds->device_mac.addr));
        ds_tree_init(&ds->fqdn_pending_reqs, dns_req_id_cmp,
                     struct fqdn_pending_req, req_node);
        ds_tree_init(&ds->dns_policy_replies_tree, dns_req_id_cmp,
                     struct fsm_policy_reply, reply_node);
        ds_tree_insert(&dns_session->session_devices, ds,
                       &ds->device_mac);
    }

    /* Check if the request is a duplicate */
    req = ds_tree_find(&ds->fqdn_pending_reqs, &dns.id);
    if (req != NULL)
    {
        LOGT("%s: request id %d already pending",
             __func__, dns.id);
        req->dedup++;
        req->timestamp = time(NULL);
        free_rrs(&dns_session->ip, &dns_session->udp, &dns, &header);
        return;
    }

    policy_reply = fsm_policy_initialize_reply(session);
    if (policy_reply == NULL)
    {
        LOGE("%s(): failed to initialize policy reply", __func__);
        return;
    }

    policy_reply->send_report = dns_session->fsm_context->ops.send_report;
    policy_reply->policy_table = session->policy_client.table;
    policy_reply->provider = session->provider;
    /* Insert the pending request */
    req = CALLOC(sizeof(*req), 1);

    memcpy(req->dev_id.addr, eth->srcmac.addr,
           sizeof(req->dev_id.addr));
    req->req_id = dns.id;
    req->dedup = 1;
    req->timestamp = time(NULL);
    req->req_info = CALLOC(sizeof(struct fsm_url_request),
                           dns.qdcount);
    req->fsm_context = dns_session->fsm_context;

    req->dev_session = ds;
    set_provider_ops(dns_session, policy_reply);
    req_info = req->req_info;
    qnext = dns.queries;
    for (i = 0; i < dns.qdcount; i++)
    {
        if ((qnext->type == 0x1) || (qnext->type == 0x1c))
        {
            STRSCPY(req_info->url, qnext->name);
            LOGT("%s: url: %s", __func__, req_info->url);
            memcpy(&req_info->dev_id, &eth->srcmac,
                   sizeof(req_info->dev_id));
            req_info->req_id = req->req_id;
            req_info++;
            cnt++;
        }
        qnext = qnext->next;
    }

    dns_session->req = req;
    req->numq = cnt;
    dns_policy_check(ds, req, policy_reply);
    free_rrs(&dns_session->ip, &dns_session->udp, &dns, &header);
}


/* Free DNS data. */
void
free_rrs(ip_info * ip, transport_info * trns, dns_info * dns,
              struct pcap_pkthdr * header)
{
    dns_question_free(dns->queries);
    dns_rr_free(dns->answers);
    dns_rr_free(dns->name_servers);
    dns_rr_free(dns->additional);
}


/* Free a dns_rr struct. */
void
dns_rr_free(dns_rr * rr)
{
    if (rr == NULL) return;
    if (rr->name != NULL) FREE(rr->name);
    if (rr->data != NULL) FREE(rr->data);
    dns_rr_free(rr->next);
    FREE(rr);
}


/* Free a dns_question struct. */
void
dns_question_free(dns_question * question)
{
    if (question == NULL) return;

    if (question->name != NULL) FREE(question->name);
    dns_question_free(question->next);
    FREE(question);
}


/*
 * Parse the questions section of the dns protocol.
 * pos - offset to the start of the questions section.
 * id_pos - offset set to the id field. Needed to decompress dns data.
 * packet, header - the packet location and header data.
 * count - Number of question records to expect.
 * root - Pointer to where to store the question records.
 */
uint32_t
parse_questions(uint32_t pos, uint32_t id_pos,
                struct pcap_pkthdr *header,
                uint8_t *packet, uint16_t count,
                dns_question ** root)
{
    uint32_t start_pos = pos;
    dns_question * last = NULL;
    dns_question * current;
    uint16_t i;

    *root = NULL;

    for (i = 0; i < count; i++)
    {
        current = MALLOC(sizeof(dns_question));
        current->next = NULL; current->name = NULL;

        current->name = read_rr_name(packet, &pos, id_pos, header->len);
        if (current->name == NULL || (pos + 2) >= header->len)
        {
            /* Handle a bad DNS name. */
            LOGD("DNS question error");
            char * buffer = escape_data(packet, start_pos, header->len);
            const char * msg = "Bad DNS question: ";
            current->name = MALLOC(sizeof(char) * (strlen(buffer) +
                                                   strlen(msg) + 1));
            sprintf(current->name, "%s%s", msg, buffer);
            FREE(buffer);
            current->type = 0;
            current->cls = 0;
            if (last == NULL) *root = current;
            else last->next = current;
            return 0;
        }
        current->type = (packet[pos] << 8) + packet[pos+1];
        current->cls = (packet[pos+2] << 8) + packet[pos+3];

        /* Add this question object to the list. */
        if (last == NULL) *root = current;
        else last->next = current;
        last = current;
        pos = pos + 4;
   }

    return pos;
}

#define plume_NULL_DOC "Plume"

/*
 * Parse an individual resource record, placing the acquired data in 'rr'.
 * 'packet', 'pos', and 'id_pos' serve the same uses as in parse_rr_set.
 * Return 0 on error, the new 'pos' in the packet otherwise.
 */
uint32_t
parse_rr(uint32_t pos, uint32_t id_pos, struct pcap_pkthdr *header,
         uint8_t *packet, dns_rr * rr)
{
    int i;
    uint32_t rr_start = pos;
    rr_parser_container * parser;
    rr_parser_container opts_cont = {0, 0, opts, "Plume", plume_NULL_DOC, 0};

    rr->name = NULL;
    rr->data = NULL;

    rr->name = read_rr_name(packet, &pos, id_pos, header->len);
    /*
     * Handle a bad rr name.
     * We still want to print the rest of the escaped rr data.
     */
    if (rr->name == NULL)
    {
        const char * msg = "Bad rr name: ";

        rr->name = MALLOC(sizeof(char) * (strlen(msg) + 1));
        sprintf(rr->name, "%s", "Bad rr name");
        rr->type = 0;
        rr->rr_name = NULL;
        rr->cls = 0;
        rr->ttl = 0;
        rr->data = escape_data(packet, pos, header->len);
        return 0;
    }

    if ((header->len - pos) < 10 ) return 0;

    rr->type = (packet[pos] << 8) + packet[pos+1];
    rr->type_pos = pos;
    rr->rdlength = (packet[pos+8] << 8) + packet[pos + 9];
    /* Handle edns opt RR's differently. */
    if (rr->type == 41)
    {
        rr->cls = 0;
        rr->ttl = 0;
        rr->rr_name = "OPTS";
        parser = &opts_cont;
        /*
         * We'll leave the parsing of the special EDNS opt fields to
         * our opt rdata parser.
         */
        pos = pos + 2;
    }
    else
    {
        /* The normal case. */
        rr->cls = (packet[pos+2] << 8) + packet[pos+3];
        rr->ttl = 0;
        for (i = 0; i < 4; i++)
        {
            rr->ttl = (rr->ttl << 8) + packet[pos+4+i];
        }
        /* Retrieve the correct parser function. */
        parser = find_parser(rr->cls, rr->type);
        rr->rr_name = parser->name;
        pos = pos + 10;
    }

    /*
     * Make sure the data for the record is actually there.
     * If not, escape and print the raw data.
     */
    if (header->len < (rr_start + 10 + rr->rdlength))
    {
        char * buffer;
        const char * msg = "Truncated rr: ";
        rr->data = escape_data(packet, rr_start, header->len);
        buffer = MALLOC(sizeof(char) * (strlen(rr->data) + strlen(msg) + 1));
        sprintf(buffer, "%s%s", msg, rr->data);
        FREE(rr->data);
        rr->data = buffer;
        return 0;
    }
    /* Parse the resource record data. */
    rr->data = parser->parser(packet, pos, id_pos, rr->rdlength,
                              header->len);
    return pos + rr->rdlength;
}


/*
 * Parse a set of resource records in the dns protocol in 'packet', starting
 * at 'pos'. The 'id_pos' offset is necessary for putting together
 * compressed names. 'count' is the expected number of records of this type.
 * 'root' is where to assign the parsed list of objects.
 * Return 0 on error, the new 'pos' in the packet otherwise.
 */
uint32_t
parse_rr_set(uint32_t pos, uint32_t id_pos,
             struct pcap_pkthdr *header,
             uint8_t *packet, uint16_t count,
             dns_rr ** root)
{
    dns_rr * last = NULL;
    dns_rr * current;
    uint16_t i;

    *root = NULL;
    for (i = 0; i < count; i++)
    {
        /* Create and clear the data in a new dns_rr object. */
        current = MALLOC(sizeof(dns_rr));
        current->next = NULL; current->name = NULL; current->data = NULL;

        pos = parse_rr(pos, id_pos, header, packet, current);
        /*
         * If a non-recoverable error occurs when parsing an rr,
         *  we can only return what we've got and give up.
         */
        if (pos == 0)
        {
            if (last == NULL) *root = current;
            else last->next = current;
            return 0;
        }
        if (last == NULL) *root = current;
        else last->next = current;
        last = current;
    }
    return pos;
}


/*
 * Parse the dns protocol in 'packet'.
 * See RFC1035
 * See dns_parse.h for more info.
 */
uint32_t
dns_parse(uint32_t pos, struct pcap_pkthdr *header,
          uint8_t *packet, dns_info *dns,
          struct dns_session *dns_session, uint8_t force)
{
    uint32_t id_pos = pos;

    if (header->len - pos < 12)
    {
        return 0;
    }

    dns->id = (packet[pos] << 8) + packet[pos+1];
    dns->qr = packet[pos+2] >> 7;
    dns->opcode = (packet[pos+2] & (0x7f)) >> 1;
    dns->AA = (packet[pos+2] & 0x04) >> 2;
    dns->TC = (packet[pos+2] & 0x02) >> 1;
    dns->Z = (packet[pos+3] >> 6) & 1;
    dns->rcode = packet[pos + 3] & 0x0f;
    /*
     * rcodes > 5 indicate various protocol errors and redefine most of the
     * remaining fields. Parsing this would hurt more than help.
     */
    if ((dns->Z != 0) || ((int16_t)(dns->opcode) > 5) ||
        ((int16_t)(dns->rcode) > 5))
    {
        LOGD("%s: ignoring request with opcode %u Z bit %u rcode %u",
             __func__, dns->opcode, dns->Z, dns->rcode);
        dns->qdcount = dns->ancount = dns->nscount = dns->arcount = 0;
        dns->queries = NULL;
        dns->answers = NULL;
        dns->name_servers = NULL;
        dns->additional = NULL;
        return pos + 12;
    }

    LOGD("%s: transaction id %d, type %s", __func__,
         dns->id, dns->qr == 0 ? "query" : "response");

    /* Counts for each of the record types. */
    dns->qdcount = (packet[pos+4] << 8) + packet[pos+5];
    dns->ancount = (packet[pos+6] << 8) + packet[pos+7];
    dns->nscount = (packet[pos+8] << 8) + packet[pos+9];
    dns->arcount = (packet[pos+10] << 8) + packet[pos+11];

    if ((dns->qdcount > 1) || (dns->ancount > 40))
    {
        LOGD("%s: ignoring request with qdcount %u ancount %u",
             __func__, dns->qdcount, dns->ancount);
        dns->qdcount = dns->ancount = dns->nscount = dns->arcount = 0;
        dns->queries = NULL;
        dns->answers = NULL;
        dns->name_servers = NULL;
        dns->additional = NULL;
        return pos + 12;
    }

    /* Parse each type of records in turn. */
    pos = parse_questions(pos+12, id_pos, header, packet,
                          dns->qdcount, &(dns->queries));
    if (pos != 0)
    {
        dns->answer_pos = pos;
        pos = parse_rr_set(pos, id_pos, header, packet,
                           dns->ancount, &(dns->answers));
    }
    else
    {
        dns->answers = NULL;
    }
    if (pos != 0 &&
        (dns_session->NS_ENABLED || dns_session->AD_ENABLED || force))
    {
        pos = parse_rr_set(pos, id_pos, header, packet,
                           dns->nscount, &(dns->name_servers));
    }
    else
    {
        dns->name_servers = NULL;
    }
    if (pos != 0 && (dns_session->AD_ENABLED || force))
    {
        pos = parse_rr_set(pos, id_pos, header, packet,
                           dns->arcount, &(dns->additional));
    }
    else
    {
        dns->additional = NULL;
    }

    return pos;
}


static void
dns_send_report(struct fqdn_pending_req *req, struct fsm_policy_reply *policy_reply)
{
    struct fsm_session *session = req->fsm_context;
    char *report;
    char *topic = session->topic;
    struct dns_session *context = session->handler_ctxt;

    if (policy_reply == NULL)
    {
        LOGE("%s(): policy reply is empty not sending dns report", __func__);
        return;
    }

    if (policy_reply->to_report != true) return;
    if (req->dns_response.num_replies > 1) return;

    if (policy_reply->action == FSM_BLOCK &&
        context->blocker_topic != NULL)
    {
        LOGT("%s: Switching topic to %s", __func__, context->blocker_topic);
        session->topic = context->blocker_topic;
    }
    report = jencode_url_report(session, req, policy_reply);
    session->ops.send_report(session, report);
    session->topic = topic;
}


static void
dns_free_req(struct fqdn_pending_req *req)
{
    struct fsm_url_request *req_info;
    int i;

    if (req->dns_reply_pkt != NULL) FREE(req->dns_reply_pkt);

    for (i = 0; i < req->dns_response.ipv4_cnt; i++) FREE(req->dns_response.ipv4_addrs[i]);

    for (i = 0; i < req->dns_response.ipv6_cnt; i++) FREE(req->dns_response.ipv6_addrs[i]);

    req_info = req->req_info;
    for (i = 0; i < req->numq; i++)
    {
        fsm_free_url_reply(req_info->reply);
        req_info++;
    }
    FREE(req->req_info);
    FREE(req);
}

void
dns_free_policy_reply(struct fsm_policy_reply *policy_reply)
{
    FREE(policy_reply->rule_name);
    FREE(policy_reply->policy);
    FREE(policy_reply);
}

void
dns_remove_policy_reply(struct dns_device *ds, uint16_t req_id)
{
    struct fsm_policy_reply *policy_reply;

    policy_reply = ds_tree_find(&ds->dns_policy_replies_tree, &req_id);
    if (policy_reply == NULL)
    {
        LOGD("%s(): could not retrieve policy reply %d",
             __func__, req_id);
        return;
    }

    ds_tree_remove(&ds->dns_policy_replies_tree, policy_reply);
    dns_free_policy_reply(policy_reply);
}

static void
dns_update_failure_count(struct dns_session *dns_session,
                         struct fsm_policy_reply *policy_reply,
                         struct fqdn_pending_req *req)
{
    if (dns_session == NULL) return;
    if (policy_reply == NULL) return;
    if (req == NULL) return;

    if (policy_reply->categorized == FSM_FQDN_CAT_FAILED
        && req->req_info->reply->lookup_status != -1)
    {
        dns_session->reported_lookup_failures++;
    }
}

void
dns_remove_req(struct dns_session *dns_session, os_macaddr_t *mac,
               uint16_t req_id)
{
    struct dns_device *ds = NULL;
    struct fqdn_pending_req *req = NULL;
    struct fsm_policy_reply *policy_reply;

    ds = ds_tree_find(&dns_session->session_devices, mac);
    if (ds == NULL)
    {
        LOGE("%s: could not find device " PRI_os_macaddr_lower_t,
             __func__, FMT_os_macaddr_pt(mac));
        return;
    }

    req = ds_tree_find(&ds->fqdn_pending_reqs, &req_id);
    if (req == NULL)
    {
        LOGD("%s: could not retrieve request %d",
             __func__, req_id);
        return;
    }

    policy_reply = ds_tree_find(&ds->dns_policy_replies_tree, &req_id);
    if (policy_reply == NULL)
    {
        LOGD("%s: could not retrieve policy reply for request %d",
             __func__, req_id);
    }

    if (req->dns_response.num_replies == 1 && policy_reply != NULL) dns_send_report(req, policy_reply);

    if (--req->dedup != 0)
    {
        LOGT("%s: req id %u has dedup value %d",
             __func__, req_id, req->dedup);
        return;
    }

    dns_update_failure_count(dns_session, policy_reply, req);

    LOGD("Removing req id %u for device " PRI_os_macaddr_lower_t,
         req_id, FMT_os_macaddr_pt(mac));

    ds_tree_remove(&ds->fqdn_pending_reqs, req);
    dns_free_req(req);

    dns_remove_policy_reply(ds, req_id);
}


bool
is_in_device_set(char values[][MAX_TAG_VALUES_LEN], int values_len, char *checking)
{
    int ret;
    int i;

    for(i = 0; i < values_len; i++)
    {
        ret = strcmp(values[i], checking);
        if (!ret) return true;
    }
    return false;
}


bool
dns_generate_update_tag(struct fqdn_pending_req *req,
                        struct fsm_policy_reply *policy_reply,
                        char values[][MAX_TAG_VALUES_LEN],
                        int *values_len, size_t max_capacity,
                        int ip_ver)
{
    bool added_information;
    om_tag_t *tag;
    char *adding;
    char name[MAX_TAG_NAME_LEN] = {0};
    int  name_len = 0;
    bool rc;
    int i;

    if (policy_reply->action != FSM_UPDATE_TAG) return false;

    added_information = false;

    if (ip_ver == 4)
    {
      name_len = strlen(policy_reply->updatev4_tag);
      os_util_strncpy(name, &policy_reply->updatev4_tag[3], name_len - 3);
      tag = om_tag_find_by_name(name, false);
    }
    else if (ip_ver == 6)
    {
      name_len = strlen(policy_reply->updatev6_tag);
      os_util_strncpy(name, &policy_reply->updatev6_tag[3], name_len - 3);
      tag = om_tag_find_by_name(name, false);
    }
    else
    {
      return false;
    }

    /* Load in current in memory tag state */
    if (tag != NULL)
    {
        om_tag_list_entry_t *iter;
        ds_tree_foreach(&tag->values, iter)
        {
            adding = iter->value;
            rc = is_in_device_set(values, *values_len, adding);
            if (!rc)
            {
                STRSCPY(values[*values_len], adding);
                *values_len = *values_len + 1;
            }
        }
    }

    if (ip_ver == 4)
    {
        for (i = 0; i < req->dns_response.ipv4_cnt; i++)
        {
            adding = req->dns_response.ipv4_addrs[i];
            rc = is_in_device_set(values, *values_len, adding);
            if (rc) continue;

            if (*values_len == (int)(max_capacity))
            {
                *values_len = *values_len % (int)max_capacity;
                LOGD("%s: tag %s max capacity reached, adding circularly at index %d",
                     __func__, name, *values_len);
            }

            STRSCPY(values[*values_len], adding);
            *values_len = *values_len + 1;
            added_information = true;
        }
    }
    else if (ip_ver == 6)
    {
        for (i = 0; i < req->dns_response.ipv6_cnt; i++)
        {
            adding = req->dns_response.ipv6_addrs[i];
            rc = is_in_device_set(values, *values_len, adding);
            if (rc) continue;

            if (*values_len == (int)(max_capacity))
            {
                *values_len = *values_len % (int)max_capacity;
                LOGD("%s: tag %s max capacity reached, adding circularly at index %d",
                     __func__, name, *values_len);
            }

            STRSCPY(values[*values_len], adding);
            *values_len = *values_len + 1;
            added_information = true;
        }
    }

    return added_information;
}



bool
dns_upsert_regular_tag(struct schema_Openflow_Tag *row, dns_ovsdb_updater updater)
{
    pjs_errmsg_t err;
    json_t *jrow;
    bool ret;

    jrow = schema_Openflow_Tag_to_json(row, err);
    if (jrow == NULL)
    {
        LOGD("%s: failed to generate JSON for upsert: %s", __func__, err);

        return false;
    }

    ret = updater(SCHEMA_TABLE(Openflow_Tag), SCHEMA_COLUMN(Openflow_Tag, name),
                  row->name, jrow, NULL);
    if (ret)
    {
        LOGD("%s: Updated Openflow_Tag: %s with new IP set.", __func__,
             row->name);
        return true;
    }
    LOGD("%s: Return value from OVSDB update was: %d", __func__, ret);

    return false;
}


bool
dns_upsert_local_tag(struct schema_Openflow_Local_Tag *row, dns_ovsdb_updater updater)
{
    pjs_errmsg_t err;
    json_t *jrow;
    bool ret;

    jrow = schema_Openflow_Local_Tag_to_json(row, err);
    if (jrow == NULL)
    {
        LOGD("%s: failed to generate JSON for upsert: %s", __func__, err);

        return false;
    }

    ret = updater(SCHEMA_TABLE(Openflow_Local_Tag),
                  SCHEMA_COLUMN(Openflow_Local_Tag, name),
                  row->name, jrow, NULL);
    if (ret)
    {
        LOGD("%s: Updated Openflow_Local_Tag: %s with new IP set.", __func__,
             row->name);
        return true;
    }
    LOGD("%s: Return value from OVSDB update was: %d", __func__, ret);

    return false;
}

void
dns_retire_reqs(struct fsm_session *session)
{
    struct dns_session *dns_session;
    struct fsm_policy_reply *policy_reply;
    struct dns_device *ds;
    struct dns_cache *mgr;
    double cmp;
    time_t now;

    mgr = dns_get_mgr();

    now = time(NULL);
    dns_session = dns_lookup_session(session);
    if (dns_session == NULL) return;

    ds = ds_tree_head(&dns_session->session_devices);
    while (ds != NULL)
    {
        struct fqdn_pending_req *req = ds_tree_head(&ds->fqdn_pending_reqs);

        while (req != NULL)
        {
            struct fqdn_pending_req *next = ds_tree_next(&ds->fqdn_pending_reqs,
                                                         req);
            struct fqdn_pending_req *remove = req;
            cmp = difftime(now, req->timestamp);

            LOGT("%s: " PRI_os_macaddr_lower_t
                 ": dns req id %d  on for %f seconds",
                 __func__, FMT_os_macaddr_t(req->dev_id), req->req_id, cmp);

            if (cmp < mgr->req_cache_ttl)
            {
                req = next;
                continue;
            }

            LOGT("%s: " PRI_os_macaddr_lower_t ": removing dns req id %d",
                 __func__, FMT_os_macaddr_t(req->dev_id), req->req_id);

            policy_reply = ds_tree_find(&ds->dns_policy_replies_tree, &req->req_id);
            if (policy_reply == NULL)
            {
                LOGD("%s(): could not find policy reply for request id %d", __func__, req->req_id);
            }
            remove = req;
            req = next;
            ds_tree_remove(&ds->fqdn_pending_reqs, remove);

            if (remove->dns_response.num_replies == 0 && policy_reply != NULL) dns_send_report(remove, policy_reply);

            dns_remove_policy_reply(ds, remove->req_id);
            dns_free_req(remove);
        }
        ds = ds_tree_next(&dns_session->session_devices, ds);
    }
}


void
dns_periodic(struct fsm_session *session)
{
    struct dns_session *dns_session;

    dns_session = dns_lookup_session(session);
    if (dns_session == NULL) return;

    /* Clean up expired ip2action entries */
    dns_cache_ttl_cleanup();
    print_dns_cache_details();

    /* Retire unresolved old requests */
    dns_retire_reqs(session);
}


/**
 * @brief: In case of gatekeeper policy, check if event
 * reporting is required. Gatekeeper policy triggers
 * reporting only for BLOCKED and REDIRECT action.
 * But if reporint is required for other action, then
 * reporting flag is set and the policy name is updated.
 */
static void
fsm_update_gk_reporting(struct fqdn_pending_req *req,
                        struct fsm_policy_req *preq,
                        struct fsm_policy_reply *policy_reply)
{
    struct fsm_policy *fsm_policy;

    fsm_policy = preq->policy;
    if (fsm_policy == NULL) return;

    if (fsm_policy->action != FSM_GATEKEEPER_REQ) return;

    LOGT("%s(): checking if policy reporting is required.", __func__);
    /* gk has already taken the action to report, no need to check
     * further.
     */
    if (policy_reply->to_report == true) return;

    /* if policy does not ask for logging, just return */
    if (preq->report == false) return;

    /* policy is set to log (example logMacs), we need
     * to send the report, also overwrite policy name
     * from gatekeeper policy (gk_all) to the policy that Requires
     * logging
     */
    LOGT("%s(): setting reporting and updating policy name", __func__);
    policy_reply->to_report = true;
    FREE(policy_reply->rule_name);
    policy_reply->rule_name = STRDUP(preq->rule_name);
    policy_reply->action = preq->action;
    policy_reply->policy_idx = preq->policy_index;
}


void
fqdn_policy_check(struct dns_device *ds,
                  struct fqdn_pending_req *req,
                  struct fsm_policy_reply *policy_reply)
{
    struct fsm_url_request *req_info = req->req_info;
    struct fsm_policy_req preq;
    struct fsm_session *session;
    struct dns_session *dns_session;
    struct fsm_url_reply *reply;
    struct web_cat_offline *offline;

    if (req->numq == 0)
    {
        policy_reply->action = FSM_FORWARD;
        return;
    }

    session = req->fsm_context;
    dns_session = session->handler_ctxt;
    LOGD("Looking up %s id %d", req->req_info[0].url,
         req->req_id);
    memset(&preq, 0, sizeof(preq));
    preq.device_id = &req->dev_id;
    preq.url = req_info->url;
    preq.fqdn_req = req;
    preq.req_type = FSM_FQDN_REQ;
    policy_reply->req_type = FSM_FQDN_REQ;
    preq.session = session;
    fsm_apply_policies(&preq, policy_reply);

    req->rd_ttl = policy_reply->rd_ttl;
    policy_reply->to_report = true;
    policy_reply->fsm_checked = true;

    /* Process reporting */
    if (policy_reply->log == FSM_REPORT_NONE)
    {
        policy_reply->to_report = false;
    }

    if ((policy_reply->log == FSM_REPORT_BLOCKED)
        && (policy_reply->action != FSM_BLOCK)
        && (policy_reply->action != FSM_REDIRECT))
    {
        policy_reply->to_report = false;
    }

    /* Overwrite logging and policy if categorization failed */
    if (policy_reply->categorized == FSM_FQDN_CAT_FAILED)
    {
        policy_reply->action = FSM_ALLOW;
        policy_reply->to_report = true;
    }

    /* Process web categorization provider connection failures */
    reply = req_info->reply;
    if (reply != NULL && reply->connection_error)
    {
        offline = &dns_session->cat_offline;
        offline->provider_offline = true;
        offline->offline_ts = time(NULL);
        offline->connection_failures++;
    }

    /* check and update reporting for gatekeeper */
    fsm_update_gk_reporting(req, &preq, policy_reply);

    LOGT("%s(): report value %d for rule: %s", __func__,
         policy_reply->to_report, policy_reply->rule_name ? policy_reply->rule_name : "None");
}

void
dns_policy_check(struct dns_device *ds,
                 struct fqdn_pending_req *req,
                 struct fsm_policy_reply *policy_reply)
{
    struct dns_cache *mgr;

    mgr = dns_get_mgr();
    mgr->policy_check(ds, req, policy_reply);
    /* policy_reply and req is mapped by req_id */
    policy_reply->req_id = req->req_id;

    /* Process the DNS reply if it was pending policy checking */
    if (req->dns_reply_pkt != NULL)
    {
        struct dns_session *dns_session = req->fsm_context->handler_ctxt;
        if (policy_reply->action != FSM_BLOCK)
        {
            mgr->forward(dns_session, NULL, req->dns_reply_pkt, req->dns_reply_pkt_len);
        }
        FREE(req->dns_reply_pkt);
    }

    LOGT("%s: redirect = %s", __func__, policy_reply->redirect ? "true" : "false");
    ds_tree_insert(&ds->fqdn_pending_reqs, req, &req->req_id);
    ds_tree_insert(&ds->dns_policy_replies_tree, policy_reply, &policy_reply->req_id);
    return;
}
