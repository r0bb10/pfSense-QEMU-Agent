<?php

if (PHP_SAPI !== 'cli') {
	http_response_code(403);
	exit;
}
require_once('/usr/local/pkg/qemu-guest-agent.inc');
require_once('/usr/local/pkg/qemu_guest_agent_update.inc');

umask(0077);
$id = $argv[1] ?? '';
if (!preg_match('/^[a-f0-9]{32}$/D', $id)) {
	exit(1);
}
// Wait for the launcher's lock handoff, then verify ownership of this request.
$lock = qemu_guest_agent_update_lock(true);
$file = QEMU_GUEST_AGENT_UPDATE_DIR . '/download-' . $id . '.pkg';
$exit_code = 0;
$state = qemu_guest_agent_update_state();
if (($state['operation_id'] ?? '') !== $id || !qemu_guest_agent_update_queued($state)) {
	qemu_guest_agent_update_unlock($lock);
	exit(1);
}
try {
	if ($state['action'] === 'check') {
		qemu_guest_agent_update_phase($state, 'checking', 'Checking the latest stable GitHub release...');
		$json = qemu_guest_agent_update_http('https://api.github.com/repos/r0bb10/pfSense-QEMU-Agent/releases/latest');
		$release = qemu_guest_agent_update_parse_release(json_decode($json, true, 512, JSON_THROW_ON_ERROR));
		$state['installed'] = qemu_guest_agent_version();
		$newer = version_compare($release['version'], $state['installed'], '>');
		$state['candidate'] = $release;
		$state['check_id'] = $id;
		$state['checked_at'] = time();
		qemu_guest_agent_update_phase($state, $newer ? 'available' : 'current', $newer ? 'QEMU Guest Agent ' . $release['version'] . ' is available.' : 'Up to date.');
	} elseif ($state['action'] === 'install' && is_array($state['candidate'] ?? null)) {
		$release = $state['candidate'];
		qemu_guest_agent_update_phase($state, 'downloading', 'Downloading QEMU Guest Agent ' . $release['version'] . '...');
		qemu_guest_agent_update_http($release['url'], $file);
		qemu_guest_agent_update_install_package($file, $release, $state);
		$state['installed'] = qemu_guest_agent_version();
		qemu_guest_agent_update_phase($state, 'current', 'Update complete. Installed QEMU Guest Agent ' . $state['installed'] . '.');
	} else {
		throw new RuntimeException('Invalid queued update action.');
	}
} catch (Throwable $error) {
	$exit_code = 1;
	qemu_guest_agent_update_phase($state, 'error', $error->getMessage());
} finally {
	if (is_file($file)) {
		unlink($file);
	}
	qemu_guest_agent_update_unlock($lock);
}
exit($exit_code);
