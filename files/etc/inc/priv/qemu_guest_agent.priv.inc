<?php

$priv_list['page-services-qemu-guest-agent'] = [];
$priv_list['page-services-qemu-guest-agent']['name'] = 'WebCfg - Services: QEMU Guest Agent';
$priv_list['page-services-qemu-guest-agent']['descr'] = 'Allow access to QEMU Guest Agent settings and status';
$priv_list['page-services-qemu-guest-agent']['match'] = [];
$priv_list['page-services-qemu-guest-agent']['match'][] = 'pkg_edit.php?xml=qemu-guest-agent.xml*';
$priv_list['page-services-qemu-guest-agent']['match'][] = 'status_qemu_guest_agent.php*';

?>
