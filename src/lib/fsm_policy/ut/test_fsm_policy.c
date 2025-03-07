/*
Copyright (c) 2015, Plume Design Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Plume Design Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>

#include "fsm.h"
#include "log.h"
#include "network_metadata_report.h"
#include "os.h"
#include "os_types.h"
#include "fsm_policy.h"
#include "target.h"
#include "unity.h"
#include "schema.h"
#include "policy_tags.h"
#include "memutil.h"

const char *test_name = "fsm_policy_tests";

struct schema_Openflow_Tag g_tags[] =
{
    {
        .name_exists = true,
        .name = "tag_1",
        .device_value_len = 2,
        .device_value =
        {
            "11:11:11:11:11:11",
            "12:12:12:12:12:12",
        },
        .cloud_value_len = 3,
        .cloud_value =
        {
            "13:13:13:13:13:13",
            "14:14:14:14:14:14",
            "15:15:15:15:15:15",
        },
    },
    {
        .name_exists = true,
        .name = "tag_2",
        .device_value_len = 2,
        .device_value =
        {
            "21:21:21:21:21:21",
            "22:22:22:22:22:22",
        },
        .cloud_value_len = 3,
        .cloud_value =
        {
            "23:23:23:23:23:23",
            "24:24:24:24:24:24",
            "25:25:25:25:25:25",
        },
    },
    {
        .name_exists = true,
        .name = "tag_3",
        .device_value_len = 2,
        .device_value =
        {
            "31:31:31:31:31:31",
            "32:32:32:32:32:32",
        },
        .cloud_value_len = 3,
        .cloud_value =
        {
            "33:33:33:33:33:33",
            "34:34:34:34:34:34",
            "35:35:35:35:35:35",
        },
    },
};


struct schema_Openflow_Tag_Group g_tag_group =
{
    .name_exists = true,
    .name = "group_tag",
    .tags_len = 2,
    .tags =
    {
        "#tag_1",
        "tag_3",
    }
};


struct schema_FSM_Policy spolicies[] =
{
    { /* entry 0 */
        .name = "DefaultTable:Rule0",
        .idx = 0,
        .mac_op_exists = true,
        .mac_op = "in",
        .macs_len = 2,
        .macs =
        {
            "Bonjour",
            "Hello",
        },
        .fqdn_op_exists = false,
        .fqdncat_op_exists = false,
        .redirect_len = 0,
    },
    { /* entry 1 used to update entry 0 - same idx, different mac values */
        .name = "Rule0",
        .idx = 0,
        .mac_op_exists = true,
        .mac_op = "in",
        .macs_len = 3,
        .macs =
        {
            "Bonjour",
            "Hello",
            "Guten Tag"
        },
        .fqdn_op_exists = false,
        .fqdncat_op_exists = false,
        .redirect_len = 0,
    },
    { /* entry 2 */
        .policy_exists = true,
        .policy = "Table1",
        .name = "Table1:Rule0",
        .idx = 0,
        .mac_op_exists = true,
        .mac_op = "in",
        .macs_len = 3,
        .macs =
        {
            "Namaste",
            "Hola",
            "Ni hao"
        },
        .fqdn_op_exists = false,
        .fqdncat_op_exists = false,
        .redirect_len = 0,
        .next_len = 1,
        .next_keys = { "Table2", },
        .next = { 1,  },
    },
    { /* entry 3 */
        .policy_exists = true,
        .policy = "dev_webpulse",
        .name = "Rule0",
        .idx = 10,
        .mac_op_exists = true,
        .mac_op = "in",
        .macs_len = 3,
        .macs =
        {
            "00:00:00:00:00:00",
            "11:22:33:44:55:66",
            "22:33:44:55:66:77"
        },
        .fqdn_op_exists = false,
        .fqdncat_op_exists = true,
        .fqdncat_op = "in",
        .fqdncats_len = 4,
        .fqdncats = { 1, 2, 10, 20 },
        .risk_op_exists = true,
        .risk_op = "gte",
        .risk_level = 6,
        .redirect_len = 2,
        .redirect = { "A-1.2.3.4", "4A-::1" },
        .next_len = 0,
        .action_exists = true,
        .action = "drop",
        .log_exists = true,
        .log = "blocked",
        .other_config_len = 1,
        .other_config_keys = { "rd_ttl", },
        .other_config = { "5" },
    },
    { /* entry 4 */
        .policy_exists = true,
        .policy = "dev_webpulse_ipthreat",
        .name = "RuleIpThreat0",
        .idx = 10,
        .mac_op_exists = false,
        .fqdn_op_exists = false,
        .fqdncat_op_exists = false,
        .risk_op_exists = true,
        .risk_op = "lte",
        .risk_level = 7,
        .ipaddr_op_exists = true,
        .ipaddr_op = "out",
        .ipaddrs_len = 2,
        .ipaddrs =
        {
            "1.2.3.4",
            "::1",
        },
    },
    { /* entry 5. Always matching, no action */
        .policy_exists = true,
        .policy = "test_policy",
        .name = "test_policy_observe",
        .idx = 0,
        .mac_op_exists = false,
        .fqdn_op_exists = false,
        .fqdncat_op_exists = false,
        .action_exists = false, /* pass through */
        .log_exists = true,
        .log = "all",
    },
    { /* entry 6. Mac match testing, block */
        .policy_exists = true,
        .policy = "mac_policy",
        .name = "mac_observe",
        .idx = 0,
        .mac_op_exists = true,
        .mac_op = "in",
        .macs_len = 3,
        .macs =
        {
            "${@tag_1}",            /* Device values */
            "$[group_tag]",
            "11:22:33:44:55:66",
        },
        .fqdn_op_exists = false,
        .fqdncat_op_exists = false,
        .action_exists = true,
        .action = "drop",
        .log_exists = true,
        .log = "all",
    },
    { /* entry 7 */
        .policy_exists = true,
        .policy = "dev_webroot",
        .name = "dev_wild_tag_update",
        .mac_op_exists = true,
        .mac_op = "in",
        .macs_len = 3,
        .macs =
        {
            "00:00:00:00:00:00",
            "11:22:33:44:55:66",
            "22:33:44:55:66:77"
        },
        .idx = 10,
        .fqdn_op_exists = true,
        .fqdn_op = "wild_in",
        .fqdns_len = 1,
        .fqdns = { "www.bo*.google.com", },
        .fqdncat_op_exists = false,
        .risk_op_exists = false,
        .ipaddr_op_exists = false,
        .ipaddrs_len = 0,
        .action_exists = true,
        .action = "update_tag",
        .other_config_len = 3,
        .other_config_keys = {"excluded_devices", "tagv4_name", "tagv6_name",},
        .other_config = {"exclude_tag", "my_v4_tag", "my_v6_tag"},
    },
    { /* entry 8 */
        .policy_exists = true,
        .policy = "dev_plume_ipthreat",
        .name = "RuleIpThreat0",
        .idx = 9,
        .mac_op_exists = false,
        .fqdn_op_exists = false,
        .fqdncat_op_exists = false,
        .risk_op_exists = false,
        .risk_op = "lte",
        .risk_level = 7,
        .ipaddr_op_exists = true,
        .ipaddr_op = "in",
        .ipaddrs_len = 2,
        .ipaddrs =
        {
            "1.2.3.4",
            "::1",
        },
        .action_exists = true,
        .action = "drop",
        .log_exists = true,
        .log = "blocked",
    },
    { /* entry 9 */
        .policy_exists = true,
        .policy = "dev_policy_flush",
        .name = "dev_rule_flush",
        .mac_op_exists = true,
        .mac_op = "in",
        .macs_len = 3,
        .macs =
        {
            "00:00:00:00:00:00",
            "11:22:33:44:55:66",
            "22:33:44:55:66:77"
        },
        .idx = 10,
        .fqdn_op_exists = true,
        .fqdn_op = "sfr_in",
        .fqdns_len = 1,
        .fqdns = { "google.com", },
        .fqdncat_op_exists = false,
        .risk_op_exists = false,
        .ipaddr_op_exists = false,
        .ipaddrs_len = 0,
        .action_exists = true,
        .action = "flush",
    },
};


void setUp(void)
{
    struct fsm_policy_session *mgr;
    size_t len;
    size_t i;
    bool ret;

    len = sizeof(g_tags) / sizeof(*g_tags);
    for (i = 0; i < len; i++)
    {
        ret = om_tag_add_from_schema(&g_tags[i]);
        TEST_ASSERT_TRUE(ret);
    }

    ret = om_tag_group_add_from_schema(&g_tag_group);
    TEST_ASSERT_TRUE(ret);

    mgr = fsm_policy_get_mgr();
    if (!mgr->initialized) fsm_init_manager();
}


void tearDown(void)
{
    struct policy_table *table, *t_to_remove;
    struct fsm_policy *fpolicy, *p_to_remove;
    ds_tree_t *tables_tree, *policies_tree;
    struct fsm_policy_session *mgr;
    size_t len;
    size_t i;
    bool ret;

    len = sizeof(g_tags) / sizeof(*g_tags);
    for (i = 0; i < len; i++)
    {
        ret = om_tag_remove_from_schema(&g_tags[i]);
        TEST_ASSERT_TRUE(ret);
    }
    ret = om_tag_group_remove_from_schema(&g_tag_group);
    TEST_ASSERT_TRUE(ret);

    mgr = fsm_policy_get_mgr();

    tables_tree = &mgr->policy_tables;
    table = ds_tree_head(tables_tree);
    while (table != NULL)
    {
        policies_tree = &table->policies;
        fpolicy = ds_tree_head(policies_tree);
        while (fpolicy != NULL)
        {
            p_to_remove = fpolicy;
            fpolicy = ds_tree_next(policies_tree, fpolicy);
            fsm_free_policy(p_to_remove);
        }
        t_to_remove = table;
        table = ds_tree_next(tables_tree, table);
        ds_tree_remove(tables_tree, t_to_remove);
        FREE(t_to_remove);
    }
}

void test_add_spolicy0(void)
{
    struct schema_FSM_Policy *spolicy;
    struct fsm_policy *fpolicy;
    struct policy_table *table;
    struct fsm_policy_rules *rules;
    struct str_set *macs_set;
    size_t i;

    /* Add the policy */
    spolicy = &spolicies[0];
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);

    /* Validate access to the fsm policy */
    TEST_ASSERT_NOT_NULL(fpolicy);

    /* Validate rule name */
    TEST_ASSERT_EQUAL_STRING(spolicy->name, fpolicy->rule_name);

    /* Validate mac content */
    rules = &fpolicy->rules;
    macs_set = rules->macs;
    TEST_ASSERT_NOT_NULL(macs_set);
    TEST_ASSERT_EQUAL_INT(spolicy->macs_len, macs_set->nelems);
    for (i = 0; i < macs_set->nelems; i++)
    {
        TEST_ASSERT_EQUAL_STRING(spolicy->macs[i], macs_set->array[i]);
    }

    /* Validate table name */
    table = fpolicy->table;
    TEST_ASSERT_EQUAL_STRING("default", table->name);

    /* Free the policy */
    fsm_delete_policy(spolicy);

    /* Add the policy and validate all over again */
    spolicy = &spolicies[0];
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);
    TEST_ASSERT_NOT_NULL(fpolicy);
    TEST_ASSERT_EQUAL_STRING(spolicy->name, fpolicy->rule_name);
    rules = &fpolicy->rules;
    macs_set = rules->macs;
    TEST_ASSERT_NOT_NULL(macs_set);
    TEST_ASSERT_EQUAL_INT(spolicy->macs_len, macs_set->nelems);
    for (i = 0; i < macs_set->nelems; i++)
    {
        TEST_ASSERT_EQUAL_STRING(spolicy->macs[i], macs_set->array[i]);
    }

    /* Check next table settings */
    TEST_ASSERT_FALSE(fpolicy->jump_table);

    /* Free the policy */
    fsm_delete_policy(spolicy);
}


void test_update_spolicy0(void)
{
    struct schema_FSM_Policy *spolicy;
    struct fsm_policy *fpolicy;
    struct fsm_policy_rules *rules;
    struct str_set *macs_set;
    size_t i;

    /* Add the policy */
    spolicy = &spolicies[0];
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);

    /* Validate access to the fsm policy */
    TEST_ASSERT_NOT_NULL(fpolicy);

    spolicy = &spolicies[1];
    fsm_update_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);

    /* Validate mac content */
    rules = &fpolicy->rules;
    macs_set = rules->macs;
    TEST_ASSERT_NOT_NULL(macs_set);
    TEST_ASSERT_EQUAL_INT(spolicy->macs_len, macs_set->nelems);
    for (i = 0; i < macs_set->nelems; i++)
    {
        LOGT("%s: mac set %zu: %s", __func__, i, macs_set->array[i]);
        TEST_ASSERT_EQUAL_STRING(spolicy->macs[i], macs_set->array[i]);
    }
    /* Free the policy */
    fsm_delete_policy(spolicy);
}


void test_add_spolicy2(void)
{
    struct schema_FSM_Policy *spolicy;
    struct fsm_policy *fpolicy;
    struct policy_table *table;
    struct fsm_policy_rules *rules;
    struct str_set *macs_set;
    size_t i;

    /* Add the policy */
    spolicy = &spolicies[2];
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);

    /* Validate access to the fsm policy */
    TEST_ASSERT_NOT_NULL(fpolicy);

    /* Validate rule name */
    TEST_ASSERT_EQUAL_STRING(spolicy->name, fpolicy->rule_name);

    /* Validate mac content */
    rules = &fpolicy->rules;
    macs_set = rules->macs;
    TEST_ASSERT_NOT_NULL(macs_set);
    TEST_ASSERT_EQUAL_INT(spolicy->macs_len, macs_set->nelems);
    for (i = 0; i < macs_set->nelems; i++)
    {
        LOGT("%s: mac set %zu: %s", __func__, i, macs_set->array[i]);
        TEST_ASSERT_EQUAL_STRING(spolicy->macs[i], macs_set->array[i]);
    }

    /* Validate table name */
    table = fpolicy->table;
    LOGT("%s: table: %s", __func__, table->name);
    TEST_ASSERT_EQUAL_STRING(spolicy->policy, table->name);

    /* Free the policy */
    fsm_delete_policy(spolicy);
}

void test_check_next_spolicy2(void)
{
    struct schema_FSM_Policy *spolicy;
    struct fsm_policy *fpolicy;
    struct policy_table *table;
    struct fsm_policy_session *mgr;

    /* Add the policy */
    spolicy = &spolicies[2];
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);

    /* Validate access to the fsm policy */
    TEST_ASSERT_NOT_NULL(fpolicy);

    /* Check next table settings */
    TEST_ASSERT_TRUE(fpolicy->jump_table);
    TEST_ASSERT_EQUAL_STRING(spolicy->next_keys[0], fpolicy->next_table);
    TEST_ASSERT_EQUAL_INT(spolicy->next[0], fpolicy->next_table_index);

    /* Check that the next table was created */
    mgr = fsm_policy_get_mgr();
    table = ds_tree_find(&mgr->policy_tables, fpolicy->next_table);
    TEST_ASSERT_NOT_NULL(table);
}

bool
test_cat_check(struct fsm_policy_req *req,
               struct fsm_policy *policy,
               struct fsm_policy_reply *policy_reply)
{
    struct fsm_policy_rules *rules;
    struct fqdn_pending_req *fqdn_req;
    struct fsm_url_request *req_info;
    struct fsm_url_reply *reply;
    bool rc;

    rules = &policy->rules;
    if (!rules->cat_rule_present) return true;

    fqdn_req = req->fqdn_req;
    req_info = fqdn_req->req_info;

    /* Allocate a reply container */
    reply = CALLOC(1, sizeof(*reply));

    req_info->reply = reply;

    reply->service_id = URL_WP_SVC;
    reply->nelems = 1;
    reply->categories[0] = 1; /* In policy blocked categories */
    reply->wb.risk_level = 1;
    policy_reply->categorized = FSM_FQDN_CAT_SUCCESS;

    rc = fsm_fqdncats_in_set(req, policy, policy_reply);
    /*
     * If category in set and policy applies to categories out of the set,
     * no match
     */
    if (rc && (rules->cat_op == CAT_OP_OUT)) return false;

    /*
     * If category not in set and policy applies to categories in the set,
     * no match
     */
    if (!rc && (rules->cat_op == CAT_OP_IN)) return false;

    return true;
}


bool
test_risk_level(struct fsm_policy_req *req,
                struct fsm_policy *policy,
                struct fsm_policy_reply *policy_reply)
{
    return true;
}


void test_apply_policies(void)
{
    struct schema_FSM_Policy *spolicy;
    struct fsm_policy *fpolicy;
    struct fsm_policy_rules *rules;
    struct int_set *fqdncats;
    struct fqdn_pending_req fqdn_req;
    struct fsm_url_request req_info;
    struct fsm_policy_req req;
    os_macaddr_t dev_mac;
    struct fsm_policy_reply *policy_reply;
    struct fsm_policy_session *mgr;
    struct policy_table *table;
    struct fsm_session session = { 0 };
    size_t i;

    /* Initialize local structures */
    memset(&fqdn_req, 0, sizeof(fqdn_req));
    memset(&req_info, 0, sizeof(req_info));
    memset(&req, 0, sizeof(req));
    memset(&dev_mac, 0, sizeof(dev_mac));

    /* Insert dev_webpulse policy */
    spolicy = &spolicies[3];
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);

    mgr = fsm_policy_get_mgr();
    table = ds_tree_find(&mgr->policy_tables, spolicy->policy);
    TEST_ASSERT_NOT_NULL(table);

    /* Validate access to the fsm policy */
    TEST_ASSERT_NOT_NULL(fpolicy);

    /* Validate fqdncats and risk level settings */
    rules = &fpolicy->rules;
    fqdncats = rules->categories;
    TEST_ASSERT_NOT_NULL(fqdncats);

    TEST_ASSERT_EQUAL_INT(spolicy->fqdncats_len, fqdncats->nelems);
    for (i = 0; i < fqdncats->nelems; i++)
    {
        LOGT("%s: category[%zu] = %d", __func__, i, fqdncats->array[i]);
        TEST_ASSERT_EQUAL_INT(spolicy->fqdncats[i], fqdncats->array[i]);
    }

    TEST_ASSERT_EQUAL_INT(spolicy->risk_level, rules->risk_level);
    req.device_id = &dev_mac;
    req.fqdn_req = &fqdn_req;

    /* Build the request elements */
    STRSCPY(req_info.url, "www.playboy.com");
    fqdn_req.req_info = &req_info;
    fqdn_req.numq = 1;
    req.fqdn_req = &fqdn_req;
    req.session = &session;

    policy_reply = fsm_policy_initialize_reply(&session);
    if (policy_reply == NULL)
    {
        LOGD("%s(): failed to initialize policy reply", __func__);
        return;
    }
    policy_reply->policy_table = table;
    policy_reply->categories_check = test_cat_check;
    policy_reply->risk_level_check = test_risk_level;
    fsm_apply_policies(&req, policy_reply);

    TEST_ASSERT_EQUAL_INT(FSM_BLOCK, policy_reply->action);
    TEST_ASSERT_EQUAL_INT(FSM_REPORT_BLOCKED, policy_reply->log);

    fsm_policy_free_reply(policy_reply);
    fsm_free_url_reply(fqdn_req.req_info->reply);
}
void test_wildcard_ovsdb_conversions(void)
{

    struct schema_FSM_Policy *spolicy = &spolicies[5];
    pjs_errmsg_t err;
    json_t *jsonrow = schema_FSM_Policy_to_json(spolicy, err);
    TEST_ASSERT_NOT_NULL(jsonrow);
    struct schema_FSM_Policy check;
    memset(&check, 0, sizeof(check));

    bool ret = schema_FSM_Policy_from_json(&check, jsonrow, false, err);
    TEST_ASSERT_TRUE(ret);

    json_decref(jsonrow);

}

void test_apply_wildcard_policy_match_in(void)
{
    struct fsm_policy_reply *policy_reply;
    struct schema_FSM_Policy *spolicy;
    struct fsm_policy *fpolicy;
    struct fsm_policy_rules *rules;
    struct fqdn_pending_req fqdn_req;
    struct fsm_url_request req_info;
    struct fsm_policy_req req;
    os_macaddr_t dev_mac;
    struct fsm_policy_session *mgr;
    struct policy_table *table;
    struct fsm_session session = { 0 };
    struct str_set *macs_set;
    struct str_set *fqdns_set;
    size_t i;

    /* Initialize local structures */
    memset(&fqdn_req, 0, sizeof(fqdn_req));
    memset(&req_info, 0, sizeof(req_info));
    memset(&req, 0, sizeof(req));
    memset(&dev_mac, 0, sizeof(dev_mac));

    /* Insert wildcard policy */
    spolicy = &spolicies[7];

    /* Validate access to the fsm policy */
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);
    TEST_ASSERT_NOT_NULL(fpolicy);

    /* Validate rule name */
    TEST_ASSERT_EQUAL_STRING(spolicy->name, fpolicy->rule_name);

    /* Validate mac content */
    rules = &fpolicy->rules;
    macs_set = rules->macs;
    TEST_ASSERT_NOT_NULL(macs_set);
    TEST_ASSERT_EQUAL_INT(spolicy->macs_len, macs_set->nelems);
    for (i = 0; i < macs_set->nelems; i++)
    {
        TEST_ASSERT_EQUAL_STRING(spolicy->macs[i], macs_set->array[i]);
    }

    /* Validate FQDNs content */
    fqdns_set = rules->fqdns;
    TEST_ASSERT_NOT_NULL(fqdns_set);

    mgr = fsm_policy_get_mgr();
    table = ds_tree_find(&mgr->policy_tables, spolicy->policy);
    TEST_ASSERT_NOT_NULL(table);

    /* Validate access to the fsm policy */
    TEST_ASSERT_NOT_NULL(fpolicy);

    /* Validate fqdncats and risk level settings */
    // rules = &fpolicy->rules;

    req.device_id = &dev_mac;

    /* Build the request elements */
    STRSCPY(req_info.url, "www.books.google.com");
    req.url = "www.books.google.com";
    fqdn_req.req_info = &req_info;
    fqdn_req.numq = 1;
    req.fqdn_req = &fqdn_req;
    req.session = &session;

    policy_reply = fsm_policy_initialize_reply(&session);
    if (policy_reply == NULL)
    {
        LOGD("%s(): failed to initialize policy reply", __func__);
        return;
    }
    policy_reply->policy_table = table;
    policy_reply->categories_check = test_cat_check;
    fsm_apply_policies(&req, policy_reply);

    /* Verify reply struct has been properly built */
    TEST_ASSERT_EQUAL_STRING("my_v4_tag", policy_reply->updatev4_tag);
    TEST_ASSERT_EQUAL_STRING("my_v6_tag", policy_reply->updatev6_tag);
    TEST_ASSERT_EQUAL_STRING("exclude_tag", policy_reply->excluded_devices);
    TEST_ASSERT_EQUAL_INT(FSM_UPDATE_TAG, policy_reply->action);

    fsm_policy_free_reply(policy_reply);
    fsm_free_url_reply(fqdn_req.req_info->reply);

}


void test_apply_wildcard_policy_no_match(void)
{
    struct schema_FSM_Policy *spolicy;
    struct fsm_policy *fpolicy;
    struct fsm_policy_rules *rules;
    struct fqdn_pending_req fqdn_req;
    struct fsm_url_request req_info;
    struct fsm_policy_req req;
    os_macaddr_t dev_mac;
    struct fsm_policy_reply *policy_reply;
    struct fsm_policy_session *mgr;
    struct policy_table *table;
    struct fsm_session session = { 0 };
    struct str_set *macs_set;
    struct str_set *fqdns_set;
    size_t i;

    /* Initialize local structures */
    memset(&fqdn_req, 0, sizeof(fqdn_req));
    memset(&req_info, 0, sizeof(req_info));
    memset(&req, 0, sizeof(req));
    memset(&dev_mac, 0, sizeof(dev_mac));

    /* Insert wildcard policy */
    spolicy = &spolicies[7];

    /* Validate access to the fsm policy */
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);
    TEST_ASSERT_NOT_NULL(fpolicy);

    /* Validate rule name */
    TEST_ASSERT_EQUAL_STRING(spolicy->name, fpolicy->rule_name);

    /* Validate mac content */
    rules = &fpolicy->rules;
    macs_set = rules->macs;
    TEST_ASSERT_NOT_NULL(macs_set);
    TEST_ASSERT_EQUAL_INT(spolicy->macs_len, macs_set->nelems);
    for (i = 0; i < macs_set->nelems; i++)
    {
        TEST_ASSERT_EQUAL_STRING(spolicy->macs[i], macs_set->array[i]);
    }

    /* Validate FQDNs content */
    fqdns_set = rules->fqdns;
    TEST_ASSERT_NOT_NULL(fqdns_set);

    mgr = fsm_policy_get_mgr();
    table = ds_tree_find(&mgr->policy_tables, spolicy->policy);
    TEST_ASSERT_NOT_NULL(table);

    /* Validate access to the fsm policy */
    TEST_ASSERT_NOT_NULL(fpolicy);

    /* Validate fqdncats and risk level settings */
    // rules = &fpolicy->rules;

    req.device_id = &dev_mac;

    /* Build the request elements */
    STRSCPY(req_info.url, "www.maps.google.com");
    req.url = "www.maps.google.com";
    fqdn_req.req_info = &req_info;
    fqdn_req.numq = 1;
    req.fqdn_req = &fqdn_req;
    req.session = &session;

    policy_reply = fsm_policy_initialize_reply(&session);
    if (policy_reply == NULL)
    {
        LOGD("%s(): failed to initialize policy reply", __func__);
        return;
    }
    policy_reply->policy_table = table;
    policy_reply->categories_check = test_cat_check;

    fsm_apply_policies(&req, policy_reply);

    /* Verify reply struct has been properly built */
    TEST_ASSERT_EQUAL_INT(FSM_NO_MATCH, policy_reply->action);
    fsm_policy_free_reply(policy_reply);
    fsm_free_url_reply(fqdn_req.req_info->reply);
}

static void test_update_client(struct fsm_session *session,
                               struct policy_table *table)
{
    struct fsm_policy_client *client;

    client = (struct fsm_policy_client *)session->handler_ctxt;
    TEST_ASSERT_NOT_NULL(client);
    client->table = table;
}

static char *
test_session_name(struct fsm_policy_client *client)
{
    if (client->name != NULL) return client->name;

    return "default_session_name";
}

void test_fsm_policy_client(void)
{
    struct fsm_session *session;
    struct schema_FSM_Policy *spolicy;
    struct fsm_policy_client *client;
    char *default_name = "default";
    struct policy_table *table;
    struct fsm_policy_session *mgr;

    /* Insert default policy */
    spolicy = &spolicies[0];
    fsm_add_policy(spolicy);

    session = CALLOC(1, sizeof(*session));

    client = CALLOC(1, sizeof(*client));
    session->handler_ctxt = client;
    client->session = session;
    client->update_client = test_update_client;
    client->session_name = test_session_name;

    /* Register the client. Its table pointer should be set */
    fsm_policy_register_client(client);
    TEST_ASSERT_NOT_NULL(client->table);

    mgr = fsm_policy_get_mgr();
    table = ds_tree_find(&mgr->policy_tables, default_name);
    TEST_ASSERT_NOT_NULL(table);
    TEST_ASSERT_TRUE(table == client->table);

    fsm_policy_deregister_client(client);
    TEST_ASSERT_NULL(client->table);

    /* Register the client with no matching policy yet */
    spolicy = &spolicies[2];
    client->name = strdup(spolicy->policy);
    client->session = session;
    TEST_ASSERT_NOT_NULL(client->name);
    fsm_policy_register_client(client);
    TEST_ASSERT_NULL(client->table);

    /* Insert matching policy */
    fsm_add_policy(spolicy);

    table = ds_tree_find(&mgr->policy_tables, spolicy->policy);
    TEST_ASSERT_NOT_NULL(table);

    /* Validate that the client's table pointer was updated */
    TEST_ASSERT_NOT_NULL(client->table);
    TEST_ASSERT_TRUE(table == client->table);
    fsm_policy_deregister_client(client);

    FREE(client->name);
    FREE(client);
    FREE(session);
}


void test_fsm_policy_clients_same_session(void)
{
    struct fsm_session *session;
    struct schema_FSM_Policy *spolicy;
    struct fsm_policy_client *default_policy_client;
    struct fsm_policy_client *dev_webpulse_client;
    char *default_name = "default";
    char *other_name = "dev_webpulse";
    struct policy_table *table;
    struct fsm_policy_session *mgr;

    /* Insert default policy */
    spolicy = &spolicies[0];
    fsm_add_policy(spolicy);

    /* Insert dev_webpulse policy */
    spolicy = &spolicies[3];
    fsm_add_policy(spolicy);

    session = CALLOC(1, sizeof(*session));

    default_policy_client = CALLOC(1, sizeof(*default_policy_client));
    session->handler_ctxt = default_policy_client;
    default_policy_client->session = session;
    default_policy_client->update_client = test_update_client;
    default_policy_client->session_name = test_session_name;

    /* Register the client. Its table pointer should be set */
    fsm_policy_register_client(default_policy_client);
    TEST_ASSERT_NOT_NULL(default_policy_client->table);

    dev_webpulse_client = CALLOC(1, sizeof(*dev_webpulse_client));
    dev_webpulse_client->name = strdup(other_name);
    TEST_ASSERT_NOT_NULL(dev_webpulse_client->name);
    session->handler_ctxt = default_policy_client;
    dev_webpulse_client->session = session;
    dev_webpulse_client->update_client = test_update_client;
    dev_webpulse_client->session_name = test_session_name;

    /* Register the client. Its table pointer should be set */
    fsm_policy_register_client(dev_webpulse_client);
    TEST_ASSERT_NOT_NULL(dev_webpulse_client->table);

    mgr = fsm_policy_get_mgr();
    table = ds_tree_find(&mgr->policy_tables, default_name);
    TEST_ASSERT_NOT_NULL(table);
    TEST_ASSERT_TRUE(table == default_policy_client->table);

    table = ds_tree_find(&mgr->policy_tables, other_name);
    TEST_ASSERT_NOT_NULL(table);
    TEST_ASSERT_TRUE(table == dev_webpulse_client->table);

    fsm_policy_deregister_client(default_policy_client);
    TEST_ASSERT_NULL(default_policy_client->table);

    fsm_policy_deregister_client(dev_webpulse_client);
    TEST_ASSERT_NULL(dev_webpulse_client->table);

    FREE(default_policy_client->name);
    FREE(default_policy_client);

    FREE(dev_webpulse_client->name);
    FREE(dev_webpulse_client);

    FREE(session);
}


/**
 * @brief test the translation of ip threat attributes from ovsdb to fsm policy
 */
void test_ip_threat_settings(void)
{
    struct schema_FSM_Policy *spolicy;
    struct fsm_policy_rules *rules;
    struct fsm_policy *fpolicy;
    struct str_set *ipaddrs;
    size_t i;

    /* Add the policy */
    spolicy = &spolicies[4];
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);

    /* Validate access to the fsm policy */
    TEST_ASSERT_NOT_NULL(fpolicy);

    /* Validate ipaddrs and risk level settings */
    rules = &fpolicy->rules;
    ipaddrs = rules->ipaddrs;
    TEST_ASSERT_NOT_NULL(ipaddrs);

    TEST_ASSERT_EQUAL_INT(spolicy->ipaddrs_len, ipaddrs->nelems);
    for (i = 0; i < ipaddrs->nelems; i++)
    {
        LOGT("%s: ipaddrs[%zu] = %s", __func__, i, ipaddrs->array[i]);
        TEST_ASSERT_EQUAL_STRING(spolicy->ipaddrs[i], ipaddrs->array[i]);
    }

    TEST_ASSERT_EQUAL_INT(spolicy->risk_level, rules->risk_level);
}


void test_apply_mac_policies(void)
{
    struct schema_FSM_Policy *spolicy;
    struct fqdn_pending_req fqdn_req;
    struct fsm_url_request req_info;
    struct fsm_policy_session *mgr;
    struct fsm_policy_rules *rules;
    struct fsm_policy_reply *policy_reply;
    struct str_set *rules_macs;
    struct fsm_session session;
    struct policy_table *table;
    struct fsm_policy *fpolicy;
    struct fsm_policy_req req;
    int expected_action;
    size_t len;
    size_t i;

    struct macs_expected
    {
        os_macaddr_t mac;
        int expected_action;
    } macs[] =
      {
          {
              .mac =
              {
                  .addr = /* In tag_1's device values */
                  {
                      0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
                  },
              },
              .expected_action = FSM_BLOCK,
          },
          {
              .mac =
              {
                  .addr = /* In tag_2's cloud values */
                  {
                      0x23, 0x23, 0x23, 0x23, 0x23, 0x23,
                  },
              },
              .expected_action = FSM_NO_MATCH,
          },
          {
              .mac =
              {
                  .addr = /* In tag_3's cloud values */
                  {
                      0x33, 0x33, 0x33, 0x33, 0x33, 0x33,
                  },
              },
              .expected_action = FSM_BLOCK,
          },
          {
              .mac =
              {
                  .addr =
                  {
                      0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                  },
              },
              .expected_action = FSM_BLOCK,
          },
      };

    /* Insert the mac match policy */
    spolicy = &spolicies[6];
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);

    mgr = fsm_policy_get_mgr();
    table = ds_tree_find(&mgr->policy_tables, spolicy->policy);
    TEST_ASSERT_NOT_NULL(table);

    /* Validate access to the fsm policy */
    TEST_ASSERT_NOT_NULL(fpolicy);

    /* Validate mac settings */
    rules = &fpolicy->rules;
    rules_macs = rules->macs;
    TEST_ASSERT_NOT_NULL(rules_macs);

        len = sizeof(macs) / sizeof(macs[0]);

    for (i = 0; i < len; i++)
    {
        /* Initialize local structures */
        memset(&fqdn_req, 0, sizeof(fqdn_req));
        memset(&req_info, 0, sizeof(req_info));
        memset(&req, 0, sizeof(req));
        memset(&session, 0, sizeof(session));

        req.fqdn_req = &fqdn_req;

        /* Build the request elements */
        STRSCPY(req_info.url, "www.playboy.com");
        fqdn_req.req_info = &req_info;
        fqdn_req.numq = 1;
        req.fqdn_req = &fqdn_req;
        req.session = &session;

        req.device_id = &macs[i].mac;
        expected_action = macs[i].expected_action;

        policy_reply = fsm_policy_initialize_reply(&session);
        if (policy_reply == NULL)
        {
            LOGD("%s(): failed to initialize policy reply", __func__);
            return;
        }

        policy_reply->policy_table = table;
        policy_reply->categories_check = test_cat_check;
        policy_reply->risk_level_check = test_risk_level;


        fsm_apply_policies(&req, policy_reply);

        TEST_ASSERT_EQUAL_INT(expected_action, policy_reply->action);

        fsm_policy_free_reply(policy_reply);
        fsm_free_url_reply(fqdn_req.req_info->reply);
    }
}


void test_apply_no_action_policy(void)
{
    struct schema_FSM_Policy *spolicy;
    struct fsm_policy *fpolicy;
    struct fqdn_pending_req fqdn_req;
    struct fsm_url_request req_info;
    struct fsm_policy_req req;
    os_macaddr_t dev_mac;
    struct fsm_policy_reply *policy_reply;
    struct fsm_policy_session *mgr;
    struct policy_table *table;
    struct fsm_session session = { 0 };

    /* Initialize local structures */
    memset(&fqdn_req, 0, sizeof(fqdn_req));
    memset(&req_info, 0, sizeof(req_info));
    memset(&req, 0, sizeof(req));
    memset(&dev_mac, 0, sizeof(dev_mac));

    /* Insert dev_webpulse policy */
    spolicy = &spolicies[5];
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);

    mgr = fsm_policy_get_mgr();
    table = ds_tree_find(&mgr->policy_tables, spolicy->policy);
    TEST_ASSERT_NOT_NULL(table);

    /* Validate access to the fsm policy */
    TEST_ASSERT_NOT_NULL(fpolicy);

    req.device_id = &dev_mac;
    req.fqdn_req = &fqdn_req;

    /* Build the request elements */
    STRSCPY(req_info.url, "www.playboy.com");
    fqdn_req.req_info = &req_info;
    fqdn_req.numq = 1;
    req.fqdn_req = &fqdn_req;
    req.session = &session;

    policy_reply = fsm_policy_initialize_reply(&session);
    if (policy_reply == NULL)
    {
        LOGD("%s(): failed to initialize policy reply", __func__);
        return;
    }
    policy_reply->policy_table = table;
    policy_reply->categories_check = test_cat_check;
    policy_reply->risk_level_check = test_risk_level;

    fsm_apply_policies(&req, policy_reply);
    TEST_ASSERT_EQUAL_INT(FSM_OBSERVED, policy_reply->action);
    TEST_ASSERT_EQUAL_INT(FSM_REPORT_ALL, policy_reply->log);

    fsm_policy_free_reply(policy_reply);
    fsm_free_url_reply(fqdn_req.req_info->reply);
}


/**
 * @brief test the translation of ip threat attributes from ovsdb to fsm policy
 */
void test_ip_threat_blacklist(void)
{
    struct net_md_stats_accumulator acc;
    struct fsm_session session = { 0 };
    struct schema_FSM_Policy *spolicy;
    struct fqdn_pending_req fqdn_req;
    struct fsm_url_request req_info;
    struct fsm_policy_session *mgr;
    struct fsm_policy_reply *policy_reply;
    struct sockaddr_storage ss_ip;
    struct net_md_flow_key key;
    struct policy_table *table;
    struct fsm_policy *fpolicy;
    struct fsm_policy_req req;
    char *ip_str = "1.2.3.4";
    struct sockaddr_in *in4;
    os_macaddr_t dev_mac;
    struct in_addr in_ip;
    int rc;

    /* Initialize local structures */
    memset(&fqdn_req, 0, sizeof(fqdn_req));
    memset(&req_info, 0, sizeof(req_info));
    memset(&req, 0, sizeof(req));
    memset(&dev_mac, 0, sizeof(dev_mac));
    memset(&ss_ip, 0, sizeof(ss_ip));
    memset(&acc, 0, sizeof(acc));
    memset(&key, 0, sizeof(key));

    /* Insert ip threat policy */
    spolicy = &spolicies[8];
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);

    mgr = fsm_policy_get_mgr();
    table = ds_tree_find(&mgr->policy_tables, spolicy->policy);
    TEST_ASSERT_NOT_NULL(table);

    /* Validate access to the fsm policy */
    TEST_ASSERT_NOT_NULL(fpolicy);

    rc = inet_pton(AF_INET, ip_str, &in_ip);
    TEST_ASSERT_EQUAL_INT(1, rc);

    in4 = (struct sockaddr_in *)&ss_ip;

    memset(in4, 0, sizeof(struct sockaddr_in));
    in4->sin_family = AF_INET;
    memcpy(&in4->sin_addr, &in_ip, sizeof(in4->sin_addr));

    fqdn_req.req_info = &req_info;
    rc = getnameinfo((struct sockaddr *)&ss_ip, sizeof(ss_ip),
                     fqdn_req.req_info->url, sizeof(fqdn_req.req_info->url),
                     0, 0, NI_NUMERICHOST);
    TEST_ASSERT_EQUAL_INT(0, rc);

    rc = strcmp(ip_str, fqdn_req.req_info->url);
    TEST_ASSERT_EQUAL_INT(0, rc);

    req.device_id = &dev_mac;
    req.fqdn_req = &fqdn_req;

    key.src_ip = (uint8_t *)&in_ip.s_addr;
    key.ip_version = 4;
    acc.key = &key;
    acc.direction = NET_MD_ACC_OUTBOUND_DIR;
    acc.originator = NET_MD_ACC_ORIGINATOR_SRC;
    /* Build the request elements */
    fqdn_req.numq = 1;
    req.fqdn_req = &fqdn_req;
    req.device_id = &dev_mac;
    req.ip_addr = &ss_ip;
    req.acc = &acc;
    req.url = fqdn_req.req_info->url;
    req.session = &session;

    policy_reply = fsm_policy_initialize_reply(&session);
    if (policy_reply == NULL)
    {
        LOGD("%s(): failed to initialize policy reply", __func__);
        return;
    }

    policy_reply->policy_table = table;
    policy_reply->categories_check = test_cat_check;
    policy_reply->risk_level_check = test_risk_level;

    fsm_apply_policies(&req, policy_reply);
    TEST_ASSERT_EQUAL_INT(FSM_BLOCK, policy_reply->action);
    TEST_ASSERT_EQUAL_INT(FSM_REPORT_BLOCKED, policy_reply->log);
    TEST_ASSERT_EQUAL_INT(9, policy_reply->policy_idx);

    fsm_policy_free_reply(policy_reply);
    fsm_free_url_reply(fqdn_req.req_info->reply);
}


char *ut_flush = "flushed";

int
ut_flush_cache(struct fsm_session *session, struct fsm_policy *policy)
{
    struct fsm_policy_client *client;

    client = &session->policy_client;
    client->name = ut_flush;

    return 0;
}


void
test_fsm_policy_flush(void)
{
    struct schema_FSM_Policy *spolicy;
    struct fsm_policy_client *client;
    struct fsm_session *session;

    session = CALLOC(1, sizeof(*session));

    client = &session->policy_client;
    client->session = session;
    client->session_name = test_session_name;
    client->flush_cache = ut_flush_cache;

    /* Register the client */
    fsm_policy_register_client(client);

    /* Insert a flush policy */
    spolicy = &spolicies[9];
    fsm_add_policy(spolicy);

    TEST_ASSERT_EQUAL(ut_flush, client->name);
    fsm_policy_deregister_client(client);
    TEST_ASSERT_NULL(client->table);

    FREE(session);
}

void
test_fsm_policy_wildmatch(void)
{
    bool rc;

    rc = fsm_policy_wildmatch("foo", "foo");
    TEST_ASSERT_TRUE(rc);

    rc = fsm_policy_wildmatch("foo.bar.com", "foo.bar.com");
    TEST_ASSERT_TRUE(rc);

    /* Too many dots */
    rc = fsm_policy_wildmatch("1.2.3.4.5.6.7.8.9.0.a", "1.2.3.4.5.6.7.8.9.0.a");
    TEST_ASSERT_FALSE(rc);

    /* wildcards */
    rc = fsm_policy_wildmatch("*", "foo");
    TEST_ASSERT_TRUE(rc);

    rc = fsm_policy_wildmatch("f*", "foo");
    TEST_ASSERT_TRUE(rc);

    rc = fsm_policy_wildmatch("f*o", "foo");
    TEST_ASSERT_TRUE(rc);

    rc = fsm_policy_wildmatch("f**", "foo");
    TEST_ASSERT_TRUE(rc);

    rc = fsm_policy_wildmatch("f**.com", "foo.com");
    TEST_ASSERT_TRUE(rc);

    rc = fsm_policy_wildmatch("foo.*", "foo.com");
    TEST_ASSERT_TRUE(rc);

    rc = fsm_policy_wildmatch("foo.bar", "foo.com");
    TEST_ASSERT_FALSE(rc);

    rc = fsm_policy_wildmatch("foo.*.com", "foo.com");
    TEST_ASSERT_FALSE(rc);
}


int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    target_log_open("TEST", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_TRACE);

    UnityBegin(test_name);

    RUN_TEST(test_add_spolicy0);
    RUN_TEST(test_update_spolicy0);
    RUN_TEST(test_add_spolicy2);
    RUN_TEST(test_check_next_spolicy2);
    RUN_TEST(test_apply_policies);
    RUN_TEST(test_wildcard_ovsdb_conversions);
    RUN_TEST(test_fsm_policy_client);
    RUN_TEST(test_ip_threat_settings);
    RUN_TEST(test_fsm_policy_clients_same_session);
    RUN_TEST(test_apply_no_action_policy);
    RUN_TEST(test_apply_mac_policies);
    RUN_TEST(test_apply_wildcard_policy_match_in);
    RUN_TEST(test_apply_wildcard_policy_no_match);
    RUN_TEST(test_ip_threat_blacklist);
    RUN_TEST(test_fsm_policy_flush);
    RUN_TEST(test_fsm_policy_wildmatch);

    return UNITY_END();
}
