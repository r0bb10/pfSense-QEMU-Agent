<?php

require_once('guiconfig.inc');
require_once('service-utils.inc');
require_once('util.inc');

if (isset($_POST['widgetkey']) || isset($_GET['widgetkey'])) {
	$requested_widgetkey = $_POST['widgetkey'] ?? $_GET['widgetkey'];
	[$widget_name, $widget_id] = array_pad(explode('-', $requested_widgetkey, 2), 2, null);
	if ($widget_name === basename(__FILE__, '.widget.php') && is_numericint($widget_id)) {
		$widgetkey = $requested_widgetkey;
	} else {
		print gettext('Invalid Widget Key');
		exit;
	}
}

if (!isset($widgetkey)) {
	print gettext('Missing Widget Key');
	exit;
}

function qemu_guest_agent_widget_escape($value) {
	return htmlspecialchars((string)$value, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
}

function qemu_guest_agent_widget_settings() {
	return config_get_path('installedpackages/qemuguestagent/config/0', []);
}

function qemu_guest_agent_widget_body() {
	$settings = qemu_guest_agent_widget_settings();
	$enabled = (($settings['enable'] ?? '') === 'on');
	$running = is_service_running('qemu-ga');
	$enabled_label = $enabled ? gettext('Yes') : gettext('No');
	$service_icon = $running ? 'fa-arrow-up text-success' : 'fa-arrow-down text-danger';
	$service_label = $running ? gettext('Running') : gettext('Stopped');
	$version = is_executable('/usr/local/bin/qemu-ga') ? trim(shell_exec('/usr/local/bin/qemu-ga --version 2>&1')) : gettext('Missing binary');
	$html = '';
	$html .= '<tr><th>' . qemu_guest_agent_widget_escape(gettext('Enabled')) . '</th><td>' . qemu_guest_agent_widget_escape($enabled_label) . '</td></tr>';
	$html .= '<tr><th>' . qemu_guest_agent_widget_escape(gettext('Service')) . '</th><td><i class="fa-solid ' . $service_icon . '"></i> ' . qemu_guest_agent_widget_escape($service_label) . '</td></tr>';
	$html .= '<tr><th>' . qemu_guest_agent_widget_escape(gettext('Version')) . '</th><td>' . qemu_guest_agent_widget_escape($version) . '</td></tr>';
	return $html;
}

if (isset($_POST['ajax'])) {
	print qemu_guest_agent_widget_body();
	exit;
}

?>
<div class="table-responsive">
	<table class="table table-striped table-hover table-condensed">
		<tbody id="<?=qemu_guest_agent_widget_escape($widgetkey)?>">
			<?=qemu_guest_agent_widget_body()?>
		</tbody>
	</table>
</div>
<div class="text-right">
	<a href="/status_qemu_guest_agent.php"><?=gettext('Full status')?></a>
</div>

<script type="text/javascript">
events.push(function() {
	function qemuGuestAgentCallback(response) {
		$(<?=json_encode('#' . $widgetkey)?>).html(response);
	}

	var refreshObject = new Object();
	refreshObject.name = 'qemu_guest_agent';
	refreshObject.url = '/widgets/widgets/qemu_guest_agent.widget.php';
	refreshObject.callback = qemuGuestAgentCallback;
	refreshObject.parms = {
		ajax: 'ajax',
		widgetkey: <?=json_encode($widgetkey)?>
	};
	refreshObject.freq = 5;
	register_ajax(refreshObject);
});
</script>
