<?php

require_once('functions.inc');
require_once('globals.inc');
require_once('service-utils.inc');
require_once('util.inc');

const QEMU_GUEST_AGENT_RCCONF = '/usr/local/etc/rc.conf.d/qemu_guest_agent';
const QEMU_GUEST_AGENT_BIN = '/usr/local/bin/qemu-ga';

function qemu_guest_agent_version() {
	if (!is_executable(QEMU_GUEST_AGENT_BIN)) {
		return 'Missing binary';
	}
	$output = [];
	$code = 0;
	exec(QEMU_GUEST_AGENT_BIN . ' --version 2>&1', $output, $code);
	if ($code !== 0 || !preg_match('/^QEMU Guest Agent ([0-9]+\.[0-9]+\.[0-9]+)$/D', trim(implode("\n", $output)), $match)) {
		throw new RuntimeException('Cannot determine QEMU Guest Agent version.');
	}
	return $match[1];
}

function qemu_guest_agent_settings() {
	return config_get_path('installedpackages/qemuguestagent/config/0', []);
}

function qemu_guest_agent_enabled() {
	$settings = qemu_guest_agent_settings();
	return (($settings['enable'] ?? '') === 'on');
}

function qemu_guest_agent_write_rcconf() {
	if (!is_dir(dirname(QEMU_GUEST_AGENT_RCCONF))) {
		mkdir(dirname(QEMU_GUEST_AGENT_RCCONF), 0755, true);
	}

	$enabled = qemu_guest_agent_enabled() ? 'YES' : 'NO';
	$rcconf = '';
	$rcconf .= "qemu_guest_agent_enable=\"{$enabled}\"\n";
	$rcconf .= "qemu_guest_agent_flags=\"-d\"\n";
	if (file_put_contents(QEMU_GUEST_AGENT_RCCONF, $rcconf, LOCK_EX) === false) {
		throw new RuntimeException('Cannot write QEMU Guest Agent service configuration.');
	}
}

function qemu_guest_agent_sync() {
	qemu_guest_agent_write_rcconf();
	if (qemu_guest_agent_enabled()) {
		if (is_service_running('qemu-ga')) {
			restart_service('qemu-ga');
		} else {
			start_service('qemu-ga');
		}
		// The daemon can return before its child has finished starting.
		for ($attempt = 0; $attempt < 50; $attempt++) {
			if (is_service_running('qemu-ga')) {
				return;
			}
			usleep(100000);
		}
		throw new RuntimeException('QEMU Guest Agent did not start. Check the VM guest-agent device.');
	} elseif (is_service_running('qemu-ga')) {
		stop_service('qemu-ga');
		if (is_service_running('qemu-ga')) {
			throw new RuntimeException('QEMU Guest Agent did not stop.');
		}
	}
}

function qemu_guest_agent_install() {
	qemu_guest_agent_register_menu();
	qemu_guest_agent_write_rcconf();
}

function qemu_guest_agent_register_menu() {
	// Upgrade old installations too: pfSense only appends menus during package registration.
	$menus = array_values(array_filter(config_get_path('installedpackages/menu', []), function ($menu) {
		return ($menu['configfile'] ?? '') !== 'qemu-guest-agent.xml' &&
			!(($menu['name'] ?? '') === 'QEMU Guest Agent' && in_array($menu['section'] ?? '', ['Services', 'Status'], true));
	}));
	$menus[] = ['name' => 'QEMU Guest Agent', 'section' => 'Services',
		'configfile' => 'qemu-guest-agent.xml', 'url' => '/qemu_guest_agent.php'];
	config_set_path('installedpackages/menu', $menus);
	foreach (config_get_path('installedpackages/package', []) as $index => $package) {
		if (($package['internal_name'] ?? '') === 'qemu-guest-agent') {
			config_del_path("installedpackages/package/{$index}/tabs");
		}
	}
}

function qemu_guest_agent_deinstall() {
	if (is_service_running('qemu-ga')) {
		stop_service('qemu-ga');
	}
	@unlink(QEMU_GUEST_AGENT_RCCONF);
}

?>
