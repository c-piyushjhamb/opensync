#!/bin/sh

# Copyright (c) 2015, Plume Design Inc. All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#    1. Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#    2. Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#    3. Neither the name of the Plume Design Inc. nor the
#       names of its contributors may be used to endorse or promote products
#       derived from this software without specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


# Include basic environment config
export FUT_WM2_LIB_SRC=true
[ "${FUT_UNIT_LIB_SRC}" != true ] && source "${FUT_TOPDIR}/shell/lib/unit_lib.sh"
echo "${FUT_TOPDIR}/shell/lib/wm2_lib.sh sourced"

####################### INFORMATION SECTION - START ###########################
#
#   Base library of common Wireless Manager functions
#
####################### INFORMATION SECTION - STOP ############################

####################### SETUP SECTION - START #################################

###############################################################################
# DESCRIPTION:
#   Function starts wireless driver on a device.
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   None.
#   See DESCRIPTION.
# NOTE:
#   This is a stub function. It always returns an error (exit 1).
#   Provide library override function for each platform.
# USAGE EXAMPLE(S):
#   start_wireless_driver
###############################################################################
start_wireless_driver()
{
    fn_name="wm2_lib:start_wireless_driver"

    # Provide override in platform specific file
    raise "FAIL: This is a stub function. Override implementation needed for specific platforms." -l "$fn_name" -fc
}

###############################################################################
# DESCRIPTION:
#   Function prepares device for WM tests.
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   None.
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   wm_setup_test_environment
###############################################################################
wm_setup_test_environment()
{
    fn_name="wm2_lib:wm_setup_test_environment"

    log "$fn_name - Running WM2 setup"

    device_init &&
        log -deb "$fn_name - Device initialized - Success" ||
        raise "FAIL: Could not initialize device: device_init" -l "$fn_name" -ds

    start_openswitch &&
        log -deb "$fn_name - OpenvSwitch started - Success" ||
        raise "FAIL: Could not start OpenvSwitch: start_openswitch" -l "$fn_name" -ds

    start_wireless_driver &&
        log -deb "$fn_name - Wireless driver started - Success" ||
        raise "FAIL: Could not start wireles driver: start_wireless_driver" -l "$fn_name" -ds

    start_specific_manager wm &&
        log -deb "$fn_name - start_specific_manager wm - Success" ||
        raise "FAIL: Could not start manager: start_specific_manager wm" -l "$fn_name" -ds

    empty_ovsdb_table AW_Debug &&
        log -deb "$fn_name - AW_Debug table emptied - Success" ||
        raise "FAIL: Could not empty table: empty_ovsdb_table AW_Debug" -l "$fn_name" -ds

    set_manager_log WM TRACE &&
        log -deb "$fn_name - Manager log for WM set to TRACE - Success" ||
        raise "FAIL: Could not set manager log severity: set_manager_log WM TRACE" -l "$fn_name" -ds

    vif_clean &&
        log -deb "$fn_name - vif_clean - Success" ||
        raise "FAIL: Could not clean VIFs: vif_clean" -l "$fn_name" -ow

    # Check if all radio interfaces are created
    for if_name in "$@"
    do
        wait_ovsdb_entry Wifi_Radio_State -w if_name "$if_name" -is if_name "$if_name" &&
            log -deb "$fn_name - Wifi_Radio_State::if_name '$if_name' present - Success" ||
            raise "FAIL: Wifi_Radio_State::if_name for $if_name does not exist" -l "$fn_name" -ds
    done

    log "$fn_name - WM setup - end"

    return 0

}
####################### SETUP SECTION - STOP ##################################

####################### VIF SECTION - START ###################################

###############################################################################
# DESCRIPTION:
#   Function empties all VIF interfaces.
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   $1  wait timeout in seconds (int, optional, default=60)
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   vif_clean
#   vif_clean 240
###############################################################################
vif_clean()
{
    fn_name="wm2_lib:vif_clean"
    VIF_CLEAN_TIMEOUT=${1:-60}
    log -deb "$fn_name - Purging VIF"

    empty_ovsdb_table Wifi_VIF_Config ||
        raise "FAIL: Could not empty table Wifi_VIF_Config: empty_ovsdb_table" -l "$fn_name" -oe

    wait_for_empty_ovsdb_table Wifi_VIF_State ${VIF_CLEAN_TIMEOUT} ||
        raise "FAIL: Could not empty table Wifi_VIF_State: wait_for_empty_ovsdb_table" -l "$fn_name" -ow
}

####################### VIF SECTION - STOP ####################################

###################### RADIO SECTION - START ##################################

###############################################################################
# DESCRIPTION:
#   Function configures existing radio interface.
#   After expansion of parameters it checks for mandatory parameters.
#   Makes sure selected channel is allowed.
#   Configures radio interface.
#   Raises an exception if radio interface does not exist, selected channel
#   is not allowed or mandatory parameters are missing.
#   Also if configuration is not reflected to Wifi_Radio_State table.
# INPUT PARAMETER(S):
#   Parameters are fed into function as key-value pairs.
#   Function supports the following keys for parameter values:
#       -if_name, -channel_mode, -fallback_parents, -ht_mode,-hw_mode,
#       -tx_chainmask, -tx_power, -enabled, -country, -channel, -timeout
#   Where mandatory key-value pair is:
#       -if_name <if_name> (string, required)
#   Other parameters are optional. Order of key-value pairs can be random.
#   Refer to USAGE EXAMPLE(S) for details.
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   configure_radio_interface -if_name wifi1 -channel 36 -enabled true
###############################################################################
configure_radio_interface()
{
    fn_name="wm2_lib:configure_radio_interface"
    radio_args=""
    replace="func_arg"
    timeout=""

    while [ -n "${1}" ]; do
        option=${1}
        shift
        case "${option}" in
            -if_name)
                radio_if_name=${1}
                radio_args="${radio_args} ${replace} ${option#?} ${1}"
                shift
                ;;
            -channel_mode | \
            -fallback_parents | \
            -ht_mode | \
            -hw_mode | \
            -tx_chainmask | \
            -tx_power | \
            -enabled)
                radio_args="${radio_args} ${replace} ${option#?} ${1}"
                shift
                ;;
            -country)
                country_arg="${replace} ${option#?} ${1}"
                radio_args="${radio_args} ${replace} ${option#?} ${1}"
                shift
                ;;
            -channel)
                radio_args="${radio_args} ${replace} ${option#?} ${1}"
                channel=$1
                shift
                ;;
            -timeout)
                timeout="-t ${1}"
                shift
                ;;
            *)
                raise "FAIL: Wrong option provided: $option" -l "$fn_name" -arg
                ;;
        esac
    done

    [ -z "${radio_if_name}" ] &&
        raise "FAIL: 'if_name' argument empty" -l "${fn_name}" -arg

    if [ -n "${channel}" ]; then
        # Only check if channel is allowed, not ready for use
        check_is_channel_allowed "$channel" "$radio_if_name" &&
            log "$fn_name - Channel $channel is allowed on $radio_if_name" ||
            raise "FAIL: Channel $channel is not allowed on $radio_if_name" -l "$fn_name" -ds
    fi

    # Perform action configure Radio
    check_ovsdb_entry Wifi_Radio_Config -w if_name "${radio_if_name}"
    [ $? -eq 0 ] ||
        raise "FAIL: Radio interface does not exits" -l "${fn_name}" -ds

    log -deb "$fn_name - Configuring radio interface"
    func_params=${radio_args//${replace}/-u}
    # shellcheck disable=SC2086
    update_ovsdb_entry Wifi_Radio_Config -w if_name "$radio_if_name" $func_params &&
        log -deb "$fn_name - Success update_ovsdb_entry Wifi_Radio_Config -w if_name $radio_if_name $func_params" ||
        raise "FAIL: Could not update_ovsdb_entry Wifi_Radio_Config -w if_name $radio_if_name $func_params" -l "$fn_name" -oe

    # Validate action configure Radio
    func_params=${radio_args//${replace}/-is}
    # shellcheck disable=SC2086
    wait_ovsdb_entry Wifi_Radio_State -w if_name "$radio_if_name" $func_params ${timeout} &&
        log -deb "$fn_name - Success wait_ovsdb_entry Wifi_Radio_State -w if_name $radio_if_name $func_params" ||
        raise "FAIL: wait_ovsdb_entry Wifi_Radio_State -w if_name $radio_if_name $func_params" -l "$fn_name" -ow

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function creates VIF interface.
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   ...
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
# Backhaul configuration on root node:
#   create_vif_interface \
#   -radio_if_name wifi1 \
#   -if_name bhaul-ap-l50 \
#   -mac_list '["set",["aa:bb:cc:dd:ee:ff"]]' \
#   -mac_list_type whitelist \
#   -mode ap \
#   -security '["map",[["encryption","WPA-PSK"],["key","PSK"],["mode","2"]]]' \
#   -ssid bhaul_ssid \
#   -ssid_broadcast "disabled" \
#   -vif_radio_idx 1 \
#   -enabled true
# Backhaul configuration on leaf node:
#   create_vif_interface \
#   -if_name bhaul-sta-l50 \
#   -security '["map",[["encryption","WPA-PSK"],["key","PSK"],["mode","2"]]]' \
#   -ssid bhaul_ssid
###############################################################################
create_vif_interface()
{
    fn_name="wm2_lib:create_vif_interface"
    vif_args_c=""
    vif_args_w=""
    replace="func_arg"

    while [ -n "${1}" ]; do
        option=${1}
        shift
        case "${option}" in
            -radio_if_name)
                radio_if_name=${1}
                shift
                ;;
            -if_name)
                vif_if_name=${1}
                vif_args_c="${vif_args_c} ${replace} ${option#?} ${1}"
                vif_args_w="${vif_args_w} ${replace} ${option#?} ${1}"
                shift
                ;;
            -ap_bridge | \
            -bridge | \
            -dynamic_beacon | \
            -mac_list_type | \
            -mac_list | \
            -parent | \
            -ssid_broadcast | \
            -vif_radio_idx | \
            -vlan_id | \
            -enabled)
                vif_args_c="${vif_args_c} ${replace} ${option#?} ${1}"
                vif_args_w="${vif_args_w} ${replace} ${option#?} ${1}"
                shift
                ;;
            -ssid)
                vif_args_c="${vif_args_c} ${replace} ${option#?} $(single_quote_arg "$1")"
                vif_args_w="${vif_args_w} ${replace} ${option#?} $(single_quote_arg "$1")"
                shift
                ;;
            -mode)
                wm2_mode=${1}
                vif_args_c="${vif_args_c} ${replace} ${option#?} ${1}"
                vif_args_w="${vif_args_w} ${replace} ${option#?} ${1}"
                shift
                ;;
            -security)
                vif_args_c="${vif_args_c} ${replace} ${option#?} $(single_quote_arg "$1")"
                vif_args_w="${vif_args_w} ${replace} ${option#?} $(single_quote_arg "$1")"
                shift
                ;;
            -credential_configs)
                vif_args_c="${vif_args_c} ${replace} ${option#?} $(single_quote_arg "$1")"
                shift
                ;;
            *)
                raise "FAIL: Wrong option provided: $option" -l "$fn_name" -arg
                ;;
        esac
    done

    [ "$wm2_mode" = "sta" ] &&
        remove_sta_connections "$vif_if_name"

    [ -z "${vif_if_name}" ] &&
        raise "FAIL: Interface name argument empty" -l "${fn_name}" -arg

    check_ovsdb_entry Wifi_VIF_Config -w if_name "${vif_if_name}"
    if [ $? -eq 0 ]; then
        log -deb "$fn_name - Updating existing VIF entry"
        function_to_call="update_ovsdb_entry"
        function_arg="-u"
    else
        log -deb "$fn_name - Creating VIF entry"
        function_to_call="insert_ovsdb_entry"
        function_arg="-i"
    fi

    # Perform action insert/update VIF
    func_params=${vif_args_c//$replace/$function_arg}
    # shellcheck disable=SC2086
    eval $function_to_call Wifi_VIF_Config -w if_name "$vif_if_name" $func_params &&
        log -deb "$fn_name - Success $function_to_call Wifi_VIF_Config -w if_name $vif_if_name $func_params" ||
        raise "FAIL: $function_to_call Wifi_VIF_Config -w if_name $vif_if_name $func_params" -l "$fn_name" -oe

    # Mutate radio entry with VIF uuid
    if [ "${function_to_call}" == "insert_ovsdb_entry" ]; then
        vif_uuid=$(get_ovsdb_entry_value Wifi_VIF_Config _uuid -w if_name "$vif_if_name" ) ||
            raise "FAIL: get_ovsdb_entry_value" -l "$fn_name" -oe
        ${OVSH} u Wifi_Radio_Config -w if_name=="${radio_if_name}" vif_configs:ins:'["set",[["uuid","'${vif_uuid//" "/}'"]]]'
    fi

    # Validate action insert/update VIF
    func_params=${vif_args_w//$replace/-is}
    # shellcheck disable=SC2086
    eval wait_ovsdb_entry Wifi_VIF_State -w if_name "$vif_if_name" $func_params &&
        log -deb "$fn_name - Success wait_ovsdb_entry Wifi_VIF_State -w if_name $vif_if_name $func_params" ||
        raise "FAIL: wait_ovsdb_entry Wifi_VIF_State -w if_name $vif_if_name $func_params" -l "$fn_name" -ow

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function creates and configures VIF interface and makes sure required
#   radio interface is created and configured as well.
#   After expansion of parameters it checks for mandatory parameters.
#   Makes sure selected channel is allowed.
#   Configures radio interface.
#   Creates (or makes an update if already exists) and configures VIF interface.
#   Makes sure all relevant Config tables are reflected to State tables.
# NOTE:
#   This function does not verify that the channel is ready for immediate
#   use, only that the channel was set, which means that DFS channels are
#   likely performing CAC, and a timeout and check needs to be done in
#   the calling function. See function: check_is_channel_ready_for_use()
# INPUT PARAMETER(S):
#   Parameters are fed into function as key-value pairs.
#   Function supports the following keys for parameter values:
#       -if_name, -vif_if_name, -vif_radio_idx, -channel
#       -channel_mode, -ht_mode, -hw_mode, -country, -enabled, -mode,
#       -ssid, -ssid_broadcast, -security, -parent, -mac_list, -mac_list_type,
#       -tx_chainmask, -tx_power, -fallback_parents,
#       -ap_bridge, -ap_bridge, -dynamic_beacon, -vlan_id,
#       -wpa, -wpa_key_mgmt, -wpa_psks, -wpa_oftags
#   Where mandatory key-value pairs are:
#       -if_name <if_name> (string, required)
#       -vif_if_name <vif_if_name> (string, required)
#       -channel <channel> (integer, required)
#   Other parameters are optional. Order of key-value pairs can be random.
#   Optional parameter pair:
#       -timeout <timeout_seconds>: how long to wait for channel change. If
#        empty, use default ovsh wait time.
#   Refer to USAGE EXAMPLE(S) for details.
# RETURNS:
#   None.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   create_radio_vif_interface -vif_radio_idx 2 \
#       -if_name wifi2 \
#       -vif_if_name home-ap-u50 \
#       -channel 165 \
#       -channel_mode manual \
#       -ssid FutTestSSID \
#       -security '["map",[["encryption","WPA-PSK"],["key","FutTestPSK"],["mode","2"]]]' \
#       -enabled true
#       -ht_mode HT20 \
#       -hw_mode 11ac \
#       -mode ap \
#       -country US
###############################################################################
create_radio_vif_interface()
{
    fn_name="wm2_lib:create_radio_vif_interface"
    vif_args_c=""
    vif_args_w=""
    radio_args=""
    replace="func_arg"
    channel_change_timeout=""

    while [ -n "$1" ]; do
        option=$1
        shift
        case "$option" in
            -ht_mode)
                radio_ht_mode="$replace ${option#?} ${1}"
                shift
                ;;
            -channel_mode | \
            -hw_mode | \
            -fallback_parents | \
            -tx_power | \
            -tx_chainmask)
                radio_args="$radio_args $replace ${option#?} ${1}"
                shift
                ;;
            -default_oftag | \
            -dpp_cc | \
            -wpa_oftags)
                vif_args_c="${vif_args_c} ${replace} ${option#?} $(single_quote_arg "$1")"
                shift
                ;;
            -vif_radio_idx | \
            -ssid_broadcast | \
            -parent | \
            -mac_list_type | \
            -dynamic_beacon | \
            -bridge | \
            -vlan_id | \
            -radius_srv_secret | \
            -radius_srv_addr | \
            -wpa | \
            -wpa_key_mgmt | \
            -wpa_psks)
                vif_args_c="${vif_args_c} ${replace} ${option#?} $(single_quote_arg "$1")"
                vif_args_w="${vif_args_w} ${replace} ${option#?} $(single_quote_arg "$1")"
                shift
                ;;
            -mac_list)
                vif_args_c="${vif_args_c} ${replace} mac_list $(single_quote_arg "$1")"
                vif_args_w="${vif_args_w} ${replace} mac_list $(single_quote_arg "$1")"
                shift
                ;;
            -credential_configs)
                vif_args_c="${vif_args_c} ${replace} credential_configs $(single_quote_arg "$1")"
                shift
                ;;
            -ssid)
                vif_args_c="${vif_args_c} ${replace} ssid $(single_quote_arg "$1")"
                vif_args_w="${vif_args_w} ${replace} ssid $(single_quote_arg "$1")"
                shift
                ;;
            -ap_bridge)
                vif_args_c="$vif_args_c $replace ap_bridge $1"
                vif_args_w="$vif_args_w $replace if_name $1"
                shift
                ;;
            -security)
                vif_args_c="$vif_args_c $replace security $(single_quote_arg "$1")"
                vif_args_w="$vif_args_w $replace security $(single_quote_arg "$1")"
                shift
                ;;
            -mode)
                vif_args_c="$vif_args_c $replace mode $1"
                vif_args_w="$vif_args_w $replace mode $1"
                wm2_mode=$1
                shift
                ;;
            -enabled)
                radio_args="$radio_args $replace enabled $1"
                vif_args_c="$vif_args_c $replace enabled $1"
                vif_args_w="$vif_args_w $replace enabled $1"
                shift
                ;;
            -country)
                radio_args="$radio_args $replace country $1"
                country_arg="$replace country $1"
                shift
                ;;
            -channel)
                radio_args="$radio_args $replace channel $1"
                vif_args_w="$vif_args_w $replace channel $1"
                channel=$1
                shift
                ;;
            -if_name)
                radio_args="$radio_args $replace if_name $1"
                wm2_if_name=$1
                shift
                ;;
            -vif_if_name)
                vif_args_c="$vif_args_c $replace if_name $1"
                vif_args_w="$vif_args_w $replace if_name $1"
                wm2_vif_if_name=$1
                shift
                ;;
            -timeout)
                channel_change_timeout="-t ${1}"
                shift
                ;;
            *)
                raise "FAIL: Wrong option provided: $option" -l "$fn_name" -arg
                ;;
        esac
    done

    [ -z "${wm2_if_name}" ] &&
        raise "FAIL: 'if_name' argument empty" -l "${fn_name}" -arg
    [ -z "${wm2_vif_if_name}" ] &&
        raise "FAIL: 'vif_if_name' argument empty" -l "${fn_name}" -arg
    [ -z "${channel}" ] &&
        raise "FAIL: 'channel' argument empty" -l "${fn_name}" -arg

    # Only check if channel is allowed, need not be ready for immediate use
    check_is_channel_allowed "$channel" "$wm2_if_name" &&
        log "$fn_name - Channel $channel is allowed on $wm2_if_name" ||
        raise "FAIL: Channel $channel is not allowed on $wm2_if_name" -l "$fn_name" -ds

    log -deb "$fn_name - Bringing up radio/vif interface"

    func_params="${radio_args//$replace/-u} ${radio_ht_mode//$replace/-u}"
    # shellcheck disable=SC2086
    update_ovsdb_entry Wifi_Radio_Config -w if_name "$wm2_if_name" $func_params &&
        log -deb "$fn_name - Table Wifi_Radio_Config updated" ||
        raise "FAIL: Could not update table Wifi_Radio_Config" -l "$fn_name" -tc

    if [ "$wm2_mode" = "sta" ]; then
        remove_sta_connections "$wm2_vif_if_name"
    fi

    function_to_call="insert_ovsdb_entry"
    function_arg="-i"

    ${OVSH} s Wifi_VIF_Config -w if_name=="$wm2_vif_if_name" &&
        update=0 ||
        update=1
    if [ "$update" -eq 0 ]; then
        log -deb "$fn_name - VIF entry exists, updating Wifi_VIF_Config instead of inserting"
        function_to_call="update_ovsdb_entry"
        function_arg="-u"
    fi

    func_params=${vif_args_c//$replace/$function_arg}
    # shellcheck disable=SC2086
    eval $function_to_call Wifi_VIF_Config -w if_name "$wm2_vif_if_name" $func_params &&
        log -deb "$fn_name - $function_to_call Wifi_VIF_Config" ||
        raise "FAIL: Could not $function_to_call to Wifi_VIF_Config" -l "$fn_name" -fc

    # Associate VIF and radio interface
    wm2_uuids=$(get_ovsdb_entry_value Wifi_VIF_Config _uuid -w if_name "$wm2_vif_if_name") ||
        raise "FAIL: Could not get _uuid for $wm2_vif_if_name from Wifi_VIF_Config: get_ovsdb_entry_value" -l "$fn_name" -oe

    wm2_vif_configs_set="[\"set\",[[\"uuid\",\"$wm2_uuids\"]]]"

    func_params=${radio_args//$replace/-u}
    # shellcheck disable=SC2086
    update_ovsdb_entry Wifi_Radio_Config -w if_name "$wm2_if_name" $func_params \
        -u vif_configs "$wm2_vif_configs_set" &&
            log -deb "$fn_name - Table Wifi_Radio_Config updated" ||
            raise "FAIL: Could not update table Wifi_Radio_Config" -l "$fn_name" -oe

    # shellcheck disable=SC2086
    func_params=${vif_args_w//$replace/-is}
    eval wait_ovsdb_entry Wifi_VIF_State -w if_name "$wm2_vif_if_name" $func_params ${channel_change_timeout} &&
        log -deb "$fn_name - Wifi_VIF_Config reflected to Wifi_VIF_State" ||
        raise "FAIL: Could not reflect Wifi_VIF_Config to Wifi_VIF_State" -l "$fn_name" -ow

    if [ -n "$country_arg" ]; then
        radio_args=${radio_args//$country_arg/""}
    fi

    # Even if the channel is set in Wifi_Radio_State, it is not
    # necessarily available for immediate use if CAC is in progress.

    func_params="${radio_args//$replace/-is} ${radio_ht_mode//$replace/-is}"

    if [ "$wm2_mode" = "sta" ]; then
        func_params="${radio_args//$replace/-is}"
    fi

    # shellcheck disable=SC2086
    wait_ovsdb_entry Wifi_Radio_State -w if_name "$wm2_if_name" $func_params ${channel_change_timeout} &&
        log -deb "$fn_name - Wifi_Radio_Config reflected to Wifi_Radio_State" ||
        raise "FAIL: Could not reflect Wifi_Radio_Config to Wifi_Radio_State" -l "$fn_name" -ow

    if [ "$wm2_mode" = "sta" ]; then
        wait_for_function_response "notempty" "get_ovsdb_entry_value Wifi_VIF_State parent -w if_name $wm2_vif_if_name" &&
            parent_mac=0 ||
            parent_mac=1
        if [ "$parent_mac" -eq 0 ]; then
            parent_mac=$(get_ovsdb_entry_value Wifi_VIF_State parent -w if_name "$wm2_vif_if_name")
            update_ovsdb_entry Wifi_VIF_Config -w if_name "$wm2_vif_if_name" \
                -u parent "$parent_mac" &&
                    log -deb "$fn_name - VIF_State parent was associated" ||
                    log -deb "$fn_name - VIF_State parent was not associated"
        fi
    fi

    log -deb "$fn_name - Wireless interface created"

    return 0
}

###############################################################################
# DESCRIPTION:
# INPUT PARAMETER(S):
# RETURNS:
# USAGE EXAMPLE(S):
###############################################################################
check_radio_vif_state()
{
    fn_name="wm2_lib:check_radio_vif_state"
    vif_args_c=""
    vif_args_w=""
    radio_args=""
    replace="func_arg"
    retval=0

    log -deb "$fn_name - Checking if interface $if_name is up"
    get_vif_interface_is_up "$if_name"
    if [ "$?" -eq 0 ]; then
        log -deb "$fn_name - Interface $if_name is up"
    else
        log -deb "$fn_name - Interface $if_name is not up"
        return 1
    fi

    while [ -n "$1" ]; do
        option=$1
        shift
        case "$option" in
            -if_name)
                radio_args="$radio_args $replace if_name $1"
                shift
                ;;
            -vif_if_name)
                vif_args="$vif_args $replace if_name $1"
                wm2_vif_if_name=$1
                shift
                ;;
            -vif_radio_idx)
                vif_args="$vif_args $replace vif_radio_idx $1"
                shift
                ;;
            -ssid)
                vif_args="$vif_args $replace ssid $(single_quote_arg "$1")"
                shift
                ;;
            -channel)
                radio_args="$radio_args $replace channel $1"
                vif_args="$vif_args $replace channel $1"
                shift
                ;;
            -ht_mode)
                radio_args="$radio_args $replace ht_mode $1"
                shift
                ;;
            -hw_mode)
                radio_args="$radio_args $replace hw_mode $1"
                shift
                ;;
            -mode)
                vif_args="$vif_args $replace mode $1"
                shift
                ;;
            -security)
                vif_args="$vif_args $replace security $(single_quote_arg "$1")"
                shift
                ;;
            -country)
                radio_args="$radio_args $replace country $1"
                shift
                ;;
            *)
                raise "FAIL: Wrong option provided: $option" -l "$fn_name" -arg
                ;;
        esac
    done

    func_params=${radio_args//$replace/-w}
    # shellcheck disable=SC2086
    check_ovsdb_entry Wifi_Radio_State $func_params
    if [ $? -eq 0 ]; then
        log -deb "$fn_name - Wifi_Radio_State is valid for given configuration"
    else
        log -deb "$fn_name - Entry with required radio arguments in Wifi_Radio_State does not exist"
        retval=1
    fi

    func_params=${vif_args//$replace/-w}
    eval check_ovsdb_entry Wifi_VIF_State $func_params
    if [ $? -eq 0 ]; then
        log -deb "$fn_name - Wifi_VIF_State is valid for given configuration"
    else
        log -deb "$fn_name - Entry with required VIF arguments in Wifi_VIF_State does not exist"
        retval=1
    fi

    return $retval
}

###############################################################################
# DESCRIPTION:
#   Function checks if channel is applied at system level.
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   $1  channel (required)
#   $2  interface name (required)
# RETURNS:
#   0   Channel is as expected.
#   See DESCRIPTION.
# NOTE:
#   This is a stub function. It always returns an error (exit 1).
#   Provide library override function for each platform.
# USAGE EXAMPLE(S):
#   check_channel_at_os_level 1 home-ap-24
###############################################################################
check_channel_at_os_level()
{
    fn_name="wm2_lib:check_channel_at_os_level"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    # shellcheck disable=SC2034
    wm2_channel=$1
    # shellcheck disable=SC2034
    wm2_vif_if_name=$2

    log -deb "$fn_name - Checking channel at OS - LEVEL2"
    # Provide override in platform specific file
    raise "FAIL: This is a stub function. Override implementation needed for specific platforms." -l "$fn_name" -fc
}

###############################################################################
# DESCRIPTION:
#   Function returns channel set at OS level - LEVEL2.
# INPUT PARAMETER(S):
#   $1  vif interface name (required)
# RETURNS:
#   0   on successful channel retrieval, fails otherwise
# NOTE:
#   This is a stub function. It always returns an error (exit 1).
#   Provide library override function for each platform.
# ECHOES:
#   Channel from OS
# USAGE EXAMPLE(S):
#   get_channel_from_os home-ap-24
###############################################################################
get_channel_from_os()
{
    fn_name="wm2_lib:get_channel_from_os"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_vif_if_name=$1

    # Provide override in platform specific file
    raise "FAIL: This is a stub function. Override implementation needed for specific platforms." -l "$fn_name" -fc
}

###############################################################################
# DESCRIPTION:
#   Function checks if HT mode for interface on selected channel is
#   applied at system level.
#   Raises exception if HT mode is not set at OS level.
# INPUT PARAMETER(S):
#   $1  HT mode (required)
#   $2  interface name (required)
#   $3  channel (required)
# RETURNS:
#   0   HT mode is as expected.
#   See DESCRIPTION.
# NOTE:
#   This is a stub function. It always returns an error (exit 1).
#   Provide library override function for each platform.
# USAGE EXAMPLE(S):
#   check_ht_mode_at_os_level HT40 home-ap-24 2
#   check_ht_mode_at_os_level HT20 home-ap-50 36
###############################################################################
check_ht_mode_at_os_level()
{
    fn_name="wm2_lib:check_ht_mode_at_os_level"
    local NARGS=3
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_ht_mode=$1
    wm2_vif_if_name=$2
    channel=$3

    log -deb "$fn_name - Checking HT MODE for channel $channel at OS level"
    # Provide override in platform specific file
    raise "FAIL: This is a stub function. Override implementation needed for specific platforms." -l "$fn_name" -fc
}

###############################################################################
# DESCRIPTION:
#   Function checks if beacon interval is applied at system level.
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   $1  beacon interval (required)
#   $2  interface name (required)
# RETURNS:
#   0   Beacon interval is as expected.
#   See DESCRIPTION.
# NOTE:
#   This is a stub function. It always returns an error (exit 1).
#   Provide library override function for each platform.
# USAGE EXAMPLE(S):
#   check_beacon_interval_at_os_level 600 home-ap-U50
###############################################################################
check_beacon_interval_at_os_level()
{
    fn_name="wm2_lib:check_beacon_interval_at_os_level"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_bcn_int=$1
    wm2_vif_if_name=$2

    log -deb "$fn_name - Checking Beacon Interval at OS - LEVEL2"
    # Provide override in platform specific file
    raise "FAIL: This is a stub function. Override implementation needed for specific platforms." -l "$fn_name" -fc
}

###############################################################################
# DESCRIPTION:
# INPUT PARAMETER(S):
# RETURNS:
# USAGE EXAMPLE(S):
###############################################################################
check_radio_mimo_config()
{
    fn_name="wm2_lib:check_radio_mimo_config"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_tx_chainmask_max_value=$1
    wm2_if_name=$2

    update_ovsdb_entry Wifi_Radio_Config -w if_name "$wm2_if_name" \
        -u tx_chainmask 0 ||
            raise "update_ovsdb_entry" -l "$fn_name" -tc

    wait_ovsdb_entry Wifi_Radio_State -w if_name "$wm2_if_name" \
        -is tx_chainmask "$wm2_tx_chainmask_max_value" &&
            log -deb "$fn_name - Max TX_CHAINMASK value is $wm2_tx_chainmask_max_value" ||
            raise "$wm2_tx_chainmask_max_value is not valid for this radio MIMO" -l "$fn_name" -tc

    mimo=$(get_ovsdb_entry_value Wifi_Radio_State tx_chainmask -w if_name "$wm2_if_name")
    case "$mimo" in
        3)
            log -deb "$fn_name - Radio MIMO config is 2x2"
            ;;
        7)
            log -deb "$fn_name - Radio MIMO config is 3x3"
            ;;
        15)
            log -deb "$fn_name - Radio MIMO config is 4x4"
            ;;
        *)
            raise "FAIL: Wrong mimo provided: $mimo" -l "$fn_name" -arg
            ;;
    esac

    return 0
}

###############################################################################
# DESCRIPTION:
# INPUT PARAMETER(S):
# RETURNS:
# USAGE EXAMPLE(S):
# NOTE:
#   This is a stub function. It always returns an error (exit 1).
#   Provide library override function for each platform.
###############################################################################
check_tx_chainmask_at_os_level()
{
    fn_name="wm2_lib:check_tx_chainmask_at_os_level"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_tx_chainmask=$1
    wm2_if_name=$2

    log -deb "$fn_name - Checking Tx Chainmask at OS level"
    # Provide override in platform specific file
    raise "FAIL: This is a stub function. Override implementation needed for specific platforms." -l "$fn_name" -fc
}

###############################################################################
# DESCRIPTION:
#   Function checks if tx power is applied at system level.
#   Provide override function if iwconfig not available on device.
# INPUT PARAMETER(S):
#   $1  tx power (required)
#   $2  VIF interface name (required)
#   $3  radio interface name (required)
# RETURNS:
#   0   Tx power is not as expected.
#   1   Tx power is as expected.
# NOTE:
#   This is a stub function. It always returns an error (exit 1).
#   Provide library override function for each platform.
# USAGE EXAMPLE(S):
#   check_tx_power_at_os_level 21 home-ap-24 wifi0
#   check_tx_power_at_os_level 14 wl0.2 wl0
###############################################################################
check_tx_power_at_os_level()
{
    fn_name="wm2_lib:check_tx_power_at_os_level"
    local NARGS=3
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    # shellcheck disable=SC2034
    wm2_tx_power=$1
    # shellcheck disable=SC2034
    wm2_vif_if_name=$2
    # shellcheck disable=SC2034
    wm2_radio_if_name=$3

    log -deb "$fn_name - Checking 'tx_power' at OS level"
    # Provide override in platform specific file
    raise "FAIL: This is a stub function. Override implementation needed for specific platforms." -l "$fn_name" -fc
}

###############################################################################
# DESCRIPTION:
#   Function returns tx_power set at OS level – LEVEL2.
#   Uses iwconfig to get tx_power info from VIF interface.
# INPUT PARAMETER(S):
#   $1  VIF interface name (required)
# RETURNS:
#   0   on successful tx_power retrieval, fails otherwise
# NOTE:
#   This is a stub function. It always returns an error (exit 1).
#   Provide library override function for each platform.
# ECHOES:
#   tx_power from OS
# USAGE EXAMPLE(S):
#   get_tx_power_from_os home-ap-24
###############################################################################
get_tx_power_from_os()
{
    fn_name="wm2_lib:get_tx_power_from_os"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_vif_if_name=$1

    # Provide override in platform specific file
    raise "FAIL: This is a stub function. Override implementation needed for specific platforms." -l "$fn_name" -fc
}

###############################################################################
# DESCRIPTION:
#   Function checks if country is applied at system level.
#   Uses iwpriv to get tx power info.
#   Provide override function if iwpriv not available on device.
#   Raises exception on fail.
# INPUT PARAMETER(S):
#   $1  country (required)
#   $2  interface name (required)
# NOTE:
#   This is a stub function. It always returns an error (exit 1).
#   Provide library override function for each platform.
# RETURNS:
#   0   Country is as expected.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   N/A, not used, awaits removal.
###############################################################################
check_country_at_os_level()
{
    fn_name="wm2_lib:check_country_at_os_level"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_country=$1
    wm2_if_name=$2

    log -deb "$fn_name - Checking 'country' at OS level - LEVEL2"
    # Provide override in platform specific file
    raise "FAIL: This is a stub function. Override implementation needed for specific platforms." -l "$fn_name" -fc
}

###############################################################################
# DESCRIPTION:
#   Function returns HT mode set at OS level - LEVEL2.
# INPUT PARAMETER(S):
#   $1  vif_if_name (required)
#   $2  channel (not used, but still required, do not optimize)
# RETURNS:
#   0   on successful channel retrieval, fails otherwise
# NOTE:
#   This is a stub function. It always returns an error (exit 1).
#   Provide library override function for each platform.
# ECHOES:
#   HT mode from OS in format: HT20, HT40 (examples)
# USAGE EXAMPLE(S):
#   get_ht_mode_from_os home-ap-24 1
###############################################################################
get_ht_mode_from_os()
{
    fn_name="wm2_lib:get_ht_mode_from_os"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_vif_if_name=$1
    wm2_channel=$2
    # Provide override in platform specific file
    raise "FAIL: This is a stub function. Override implementation needed for specific platforms." -l "$fn_name" -fc
}

###############################################################################
# DESCRIPTION:
#   Function echoes the radio channel state description in channels field of
#   table Wifi_Radio_State.
# INPUT PARAMETER(S):
#   $1  channel (required)
#   $2  radio interface name (required)
# RETURNS:
#   0   A valid channel state was echoed to stdout
#   1   Channel is not allowed on radio
#       Other errors during state description parsing
# ECHOES (if return 0):
#   "allowed"       : non-dfs channel
#   "nop_finished"  : dfs channel, requires cac before using
#   "cac_completed" : dfs channel, cac completed, usable
#   "nop_started"   : dfs channel, radar was detected and it must not be used
# USAGE EXAMPLE(S):
#   ch_state=$(get_radio_channel_state 2 wifi0)
###############################################################################
get_radio_channel_state()
{
    fn_name="wm2_lib:get_radio_channel_state"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    channel=$1
    radio_if_name=$2

    # Ensure channel is allowed. Redirect output to ensure clean echo to stdout
    check_is_channel_allowed "$channel" "$radio_if_name" >/dev/null 2>&1 ||
        return 1

    state_raw=$(get_ovsdb_entry_value Wifi_Radio_State channels -w if_name "$radio_if_name" -raw | tr ']' '\n' | grep "$channel")
    state="$(echo "${state_raw##*state}" | tr -d ' \":}')"
    if [ "$state" == "allowed" ]; then
        echo "allowed"
    elif [ "$state" == "nop_finished" ]; then
        echo "nop_finished"
    elif [ "$state" == "cac_completed" ]; then
        echo "cac_completed"
    elif [ "$state" == "nop_started" ]; then
        echo "nop_started"
    else
        # Undocumented state, return 1
        echo "${state_raw##*state}" | tr -d '\":}'
        return 1
    fi

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function checks if requested channel is available on radio interface.
#   It does not check if the channel is available for immediate use.
# INPUT PARAMETER(S):
#   $1  channel (required)
#   $2  radio interface name (required)
# RETURNS:
#   0   Channel is available on this radio.
#   !=0 Channel is not available on this radio.
# USAGE EXAMPLE(S):
#   check_is_channel_allowed 2 wifi0
#   check_is_channel_allowed 144 wifi2
###############################################################################
check_is_channel_allowed()
{
    fn_name="wm2_lib:check_is_channel_allowed"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    channel=$1
    radio_if_name=$2

    log -deb "$fn_name - Waiting for Wifi_Radio_State::allowed_channels to be populated"
    wait_for_function_response 'notempty' "get_ovsdb_entry_value Wifi_Radio_State allowed_channels -w if_name ${radio_if_name}" &&
        log -deb "$fn_name - Wifi_Radio_State::allowed_channels populated" ||
        raise "FAIL: Wifi_Radio_State::allowed_channels not populated" -l "$fn_name" -ds
    log -deb "$fn_name - Checking is channel $channel allowed for $radio_if_name"
    allowed_channels=$(get_ovsdb_entry_value Wifi_Radio_State allowed_channels -w if_name "$radio_if_name" -raw)
    if [ -z "${allowed_channels}" ]; then
        ${OVSH} s Wifi_Radio_State
        raise "FAIL: Wifi_Radio_State::allowed_channels for $radio_if_name is empty" -l "$fn_name" -ds
    fi

    echo "$allowed_channels" | grep -qF "$wm2_channel" &&
        log -deb "$fn_name - Channel $channel is allowed on radio $radio_if_name" ||
        raise "FAIL: Wifi_Radio_State::allowed_channels for $radio_if_name does not contain $channel" -l "$fn_name" -ds

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function checks if a radar event was detected on the requested channel for
#   the requested radio interface.
# INPUT PARAMETER(S):
#   $1  channel (required)
#   $2  radio interface name (required)
# RETURNS:
#   0   No radar detected on channel.
#   1   Radar detected on channel.
# USAGE EXAMPLE(S):
#   check_radar_event_on_channel 2 wifi0
###############################################################################
check_radar_event_on_channel()
{
    fn_name="wm2_lib:check_radar_event_on_channel"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    channel=$1
    radio_if_name=$2

    check_is_channel_allowed "$channel" "$radio_if_name" ||
        raise "FAIL: Channel $channel is not allowed on radio $radio_if_name" -l "$fn_name" -ds

    log -deb "$fn_name - Checking radar events on channel $channel"
    if [ "$(get_radio_channel_state "$channel" "$radio_if_name")" == "nop_started" ]; then
        raise "FAIL: Radar event detected on channel $channel" -f "$fn_name" -ds
    fi

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function checks if requested channel is ready to use on radio interface.
#   Even if the channel changes in Wifi_Radio_State table, this might mean that
#   the channel was DFS and is currently undergoing CAC. THe channel is actually
#   ready for use, only once the state is equal to "allowed" or "cac_completed".
# INPUT PARAMETER(S):
#   $1  channel (required)
#   $2  radio interface name (required)
# RETURNS:
#   0   Channel is ready use.
#   1   Channel is not ready to use.
# USAGE EXAMPLE(S):
#   check_is_channel_ready_for_use 2 wifi0
###############################################################################
check_is_channel_ready_for_use()
{
    fn_name="wm2_lib:check_is_channel_ready_for_use"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_channel=$1
    wm2_if_name=$2
    is_empty=false

    log -deb "$fn_name - Checking if channel $wm2_channel ready for immediate use"
    wait_for_function_response "notempty" "get_ovsdb_entry_value Wifi_Radio_State channels -w if_name $wm2_if_name" || is_empty=true

    if [ "$is_empty" = "true" ]; then
        log -deb "$fn_name - Table Wifi_Radio_State dump"
        ${OVSH} s Wifi_Radio_State || true
        raise "FAIL: Wifi_Radio_State::channels is empty for $if_name" -l "$fn_name" -ds
    fi

    check_is_channel_allowed "$wm2_channel" "$wm2_if_name" &&
        log -deb "$fn_name - channel $wm2_channel is allowed on radio $wm2_if_name" ||
        raise "FAIL: Channel $wm2_channel is not allowed on radio $wm2_if_name" -l "$fn_name" -ds

    state="$(get_radio_channel_state "$wm2_channel" "$wm2_if_name")"
    if [ "$state" == "cac_completed" ] || [ "$state" == "allowed" ]; then
        log -deb "$fn_name - Channel $wm2_channel is ready for use - $state"
        return 0
    fi

    log -deb "$fn_name - Channel $wm2_channel is not ready for use: $state"
    return 1
}

###############################################################################
# DESCRIPTION:
#   Function checks if CAC (Channel Availability Check) on channel started.
#   Raises exception if CAC not started.
# INPUT PARAMETER(S):
#   $1  channel (required)
#   $2  interface name (required)
# RETURNS:
#   0   CAC started for channel.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   check_is_cac_started 120 wifi2
#   check_is_cac_started 100 wifi2
###############################################################################
check_is_cac_started()
{
    fn_name="wm2_lib:check_is_cac_started"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_channel=$1
    wm2_if_name=$2

    log -deb "$fn_name - Checking if CAC is started on channel $wm2_channel"
    if ${OVSH} s Wifi_Radio_State channels -w if_name=="$wm2_if_name" -r | grep -qF '["'$wm2_channel'","{\"state\": \"cac_started\"}"]'; then
        log -deb "$fn_name - CAC started on channel $wm2_channel"
        return 0
    fi

    ${OVSH} s Wifi_Radio_State channels -w if_name=="$wm2_if_name" || true
    raise "FAIL: CAC is not started on channel $wm2_channel" -l "$fn_name" -tc
}

###############################################################################
# DESCRIPTION:
#   Function checks if NOP (No Operation) on channel is finished.
#   Raises exception if NOP not finished.
# INPUT PARAMETER(S):
#   $1  channel (required)
#   $2  interface name (required)
# RETURNS:
#   0   NOP finished for channel or channel is allowed.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   check_is_nop_finished 120 wifi2
###############################################################################
check_is_nop_finished()
{
    fn_name="wm2_lib:check_is_nop_finished"
    local NARGS=2
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_channel=$1
    wm2_if_name=$2

    log -deb "$fn_name - Checking if NOP finished on channel $wm2_channel"
    if ${OVSH} s Wifi_Radio_State channels -w if_name=="$wm2_if_name" -r | grep -qF '["'$wm2_channel'","{\"state\": \"nop_finished\"}"]'; then
        log -deb "$fn_name - NOP finished on channel $wm2_channel"
        return 0
    elif
        ${OVSH} s Wifi_Radio_State channels -w if_name=="$wm2_if_name" -r | grep -qF '["'$wm2_channel'","{\"state\":\"allowed\"}"]'; then
        log -deb "$fn_name - Channel $wm2_channel is allowed"
        return 0
    fi

    ${OVSH} s Wifi_Radio_State channels -w if_name=="$wm2_if_name" || true
    raise "FAIL: NOP is not finished on channel $wm2_channel" -l "$fn_name" -tc
}

###############################################################################
# DESCRIPTION:
#   Function simulates DFS (Dynamic Frequency Shift) radar event on interface.
# INPUT PARAMETER(S):
#   $1  channel (required)
# RETURNS:
#   0   Simulation was a success.
# NOTE:
#   This is a stub function. It always returns an error (exit 1).
#   Provide library override function for each platform.
# USAGE EXAMPLE(S):
#   N/A
###############################################################################
simulate_dfs_radar()
{
    fn_name="wm2_lib:simulate_dfs_radar"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_if_name=$1

    log -deb "$fn_name - Triggering DFS radar event on ${wm2_if_name}"
    # Provide override in platform specific file
    raise "FAIL: This is a stub function. Override implementation needed for specific platforms." -l "$fn_name" -fc
}

###################### RADIO SECTION - STOP ###################################

###################### STATION SECTION - START ################################

###############################################################################
# DESCRIPTION:
# INPUT PARAMETER(S):
# RETURNS:
# USAGE EXAMPLE(S):
###############################################################################
remove_sta_connections()
{
    fn_name="wm2_lib:remove_sta_connections"
    local NARGS=1
    [ $# -ne ${NARGS} ] &&
        raise "${fn_name} requires ${NARGS} input argument(s), $# given" -arg
    wm2_sta_if_name=$1

    log -deb "[DEPRECATED] - Function ${fn_name} is deprecated in favor of remove_sta_interfaces_exclude"
    log -deb "$fn_name - Removing sta connections except $wm2_sta_if_name"
    ${OVSH} d Wifi_VIF_Config -w if_name!="$wm2_sta_if_name" -w mode==sta &&
        log -deb "$fn_name - sta connections except $wm2_sta_if_name removed" ||
        raise "FAIL: Could not remove STA interfaces" -l "$fn_name" -oe

    return 0
}

###############################################################################
# DESCRIPTION:
#   Function removes all STA interfaces, except explicitly provided ones.
#   Waits timeout time for interfaces to be removed.
#   Waits for system to react, or timeouts with error.
# INPUT PARAMETER(S):
#   $1  wait timeout in seconds (int, optional, default=DEFAULT_WAIT_TIME)
#   $2  sta interface name, interface to keep from removing (optional)
# RETURNS:
#   0   On success.
#   See DESCRIPTION.
# USAGE EXAMPLE(S):
#   remove_sta_interfaces_exclude 60
###############################################################################
remove_sta_interfaces_exclude()
{
    local fn_name="wm2_lib:remove_sta_interfaces_exclude"
    local wait_timeout=${1:-$DEFAULT_WAIT_TIME}
    local wm2_sta_if_name=$2
    local retval=1

    if [ -n "${wm2_sta_if_name}" ]; then
        log -deb "$fn_name - Removing STA interfaces except ${wm2_sta_if_name}"
        ovs_cmd="-w mode==sta -w if_name!=${wm2_sta_if_name}"
    else
        log -deb "$fn_name - Removing all STA interfaces"
        ovs_cmd="-w mode==sta"
    fi

    # shellcheck disable=SC2086
    ${OVSH} d Wifi_VIF_Config ${ovs_cmd} &&
        log -deb "$fn_name - Removed STA interfaces from Wifi_VIF_Config" ||
        raise "Failed to remove STA interfaces from Wifi_VIF_Config" -l "$fn_name" -oe

    wait_time=0
    while [ $wait_time -le "$wait_timeout" ]; do
        wait_time=$((wait_time+1))

        log -deb "$fn_name - Waiting for Wifi_VIF_State table, retry $wait_time"
        # shellcheck disable=SC2086
        table_select=$(${OVSH} s Wifi_VIF_State ${ovs_cmd}) || true
        if [ -z "$table_select" ]; then
            retval=0
            break
        fi

        sleep 1
    done

    if [ $retval = 0 ]; then
        log -deb "$fn_name - Removed STA interfaces from Wifi_VIF_State"
        return $retval
    else
        raise "FAIL: Could not remove STA interfaces from Wifi_VIF_State" -l "$fn_name" -oe
    fi
}

############################################ STATION SECTION - STOP ####################################################
