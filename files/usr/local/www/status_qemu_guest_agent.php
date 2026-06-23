<?php

require_once('guiconfig.inc');
require_once('service-utils.inc');

const QEMU_GUEST_AGENT_BIN = '/usr/local/bin/qemu-ga';
const QEMU_GUEST_AGENT_LOG = '/var/log/qemu-ga.log';

function qemu_guest_agent_status_escape($value) {
	return htmlspecialchars((string)$value, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
}

function qemu_guest_agent_command_output($command) {
	$output = [];
	$result = 0;
	exec($command . ' 2>&1', $output, $result);
	return trim(implode("\n", $output));
}

function qemu_guest_agent_settings() {
	return config_get_path('installedpackages/qemuguestagent/config/0', []);
}

if (isset($_GET['ajax'])) {
	header('Content-Type: application/json');
	header('Cache-Control: no-store');
	$status = [
		'enabled' => ((qemu_guest_agent_settings()['enable'] ?? '') === 'on'),
		'running' => is_service_running('qemu-ga'),
		'version' => is_executable(QEMU_GUEST_AGENT_BIN) ? qemu_guest_agent_command_output(QEMU_GUEST_AGENT_BIN . ' --version') : 'qemu-ga binary is missing',
		'log' => is_readable(QEMU_GUEST_AGENT_LOG) ? implode("\n", array_slice(file(QEMU_GUEST_AGENT_LOG, FILE_IGNORE_NEW_LINES), -30)) : '',
	];
	echo json_encode($status);
	exit;
}

$shortcut_section = 'qemu_guest_agent';
$pgtitle = [gettext('Status'), gettext('QEMU Guest Agent')];
include('head.inc');
?>

<ul class="nav nav-tabs">
	<li><a href="/pkg_edit.php?xml=qemu-guest-agent.xml"><?=gettext('Settings')?></a></li>
	<li class="active"><a href="/status_qemu_guest_agent.php"><?=gettext('Status')?></a></li>
</ul>

<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title"><?=gettext('Service')?></h2></div>
	<div class="panel-body">
		<table class="table table-striped table-condensed">
			<tbody>
				<tr><th><?=gettext('Enabled')?></th><td id="qemu-guest-agent-enabled">-</td></tr>
				<tr><th><?=gettext('State')?></th><td id="qemu-guest-agent-state">-</td></tr>
				<tr><th><?=gettext('Version')?></th><td id="qemu-guest-agent-version">-</td></tr>
			</tbody>
		</table>
	</div>
</div>

<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title"><?=gettext('Recent Log')?></h2></div>
	<div class="panel-body">
		<pre id="qemu-guest-agent-log" style="max-height: 30em; overflow: auto;">-</pre>
	</div>
</div>

<script>
async function refreshQemuGuestAgent() {
	const response = await fetch('/status_qemu_guest_agent.php?ajax=1', {cache: 'no-store'});
	const data = await response.json();
	document.getElementById('qemu-guest-agent-enabled').textContent = data.enabled ? 'Yes' : 'No';
	document.getElementById('qemu-guest-agent-state').textContent = data.running ? 'Running' : 'Stopped';
	document.getElementById('qemu-guest-agent-version').textContent = data.version || '-';
	document.getElementById('qemu-guest-agent-log').textContent = data.log || 'No readable log output.';
}
refreshQemuGuestAgent();
setInterval(refreshQemuGuestAgent, 5000);
</script>

<?php include('foot.inc'); ?>
