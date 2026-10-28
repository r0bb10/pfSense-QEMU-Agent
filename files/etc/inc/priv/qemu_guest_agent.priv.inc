<?php

$priv_list['page-services-qemu-guest-agent'] = [];
$priv_list['page-services-qemu-guest-agent']['name'] = 'WebCfg - Services: QEMU Guest Agent';
$priv_list['page-services-qemu-guest-agent']['descr'] = 'Allow access to QEMU Guest Agent service settings and status';
$priv_list['page-services-qemu-guest-agent']['match'] = [];
$priv_list['page-services-qemu-guest-agent']['match'][] = 'qemu_guest_agent.php*';

$priv_list['page-services-qemu-guest-agent-update'] = [
	'name' => 'WebCfg - Services: QEMU Guest Agent: Update',
	'descr' => 'Allow installation of QEMU Guest Agent packages from GitHub as root',
	'match' => ['qemu_guest_agent-update'],
];

?>
