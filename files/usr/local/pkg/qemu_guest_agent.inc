<?php

require_once('functions.inc');
require_once('globals.inc');
require_once('service-utils.inc');
require_once('util.inc');

const QEMU_GUEST_AGENT_RCCONF = '/usr/local/etc/rc.conf.d/qemu_guest_agent';
const QEMU_GUEST_AGENT_LOG = '/var/log/qemu-ga.log';

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
	$rcconf .= 'qemu_guest_agent_flags="-d -v -l ' . QEMU_GUEST_AGENT_LOG . "\"\n";
	file_put_contents(QEMU_GUEST_AGENT_RCCONF, $rcconf, LOCK_EX);
}

function qemu_guest_agent_sync() {
	qemu_guest_agent_write_rcconf();
	if (qemu_guest_agent_enabled()) {
		if (is_service_running('qemu-ga')) {
			restart_service('qemu-ga');
		} else {
			start_service('qemu-ga');
		}
	} elseif (is_service_running('qemu-ga')) {
		stop_service('qemu-ga');
	}
}

function qemu_guest_agent_install() {
	qemu_guest_agent_write_rcconf();
}

function qemu_guest_agent_deinstall() {
	if (is_service_running('qemu-ga')) {
		stop_service('qemu-ga');
	}
	@unlink(QEMU_GUEST_AGENT_RCCONF);
}

?>
