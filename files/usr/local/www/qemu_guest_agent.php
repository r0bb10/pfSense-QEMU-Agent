<?php

require_once('guiconfig.inc');
require_once('/usr/local/pkg/qemu-guest-agent.inc');
require_once('/usr/local/pkg/qemu_guest_agent_update.inc');

$can_update = qemu_guest_agent_can_update();
$input_errors = [];
$automatic = $_SERVER['REQUEST_METHOD'] === 'POST' && isset($_POST['autocheck']);
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
	$lock = null;
	$redirect = false;
	try {
		$actions = array_intersect(['save', 'install', 'autocheck'], array_keys($_POST));
		if (count($actions) !== 1) {
			throw new RuntimeException('Invalid action.');
		}
		if (isset($_POST['save'])) {
			$lock = qemu_guest_agent_update_lock();
			if (qemu_guest_agent_update_queued(qemu_guest_agent_update_state()) ||
				is_subsystem_dirty('pkg') || is_subsystem_dirty('packagelock')) {
				throw new RuntimeException('An update or package operation is in progress. Wait before saving.');
			}
			if (!config_read_file()) {
				throw new RuntimeException('Cannot read pfSense configuration.');
			}
			$settings = qemu_guest_agent_settings();
			$previous = $settings;
			$settings['enable'] = isset($_POST['enable']) ? 'on' : '';
			config_set_path('installedpackages/qemuguestagent/config/0', $settings);
			write_config('QEMU Guest Agent: changed settings');
			if ($settings !== $previous) {
				qemu_guest_agent_sync();
			}
			$redirect = true;
		} else {
			if (!$can_update) {
				http_response_code(403);
				throw new RuntimeException('The QEMU Guest Agent Update privilege is required.');
			}
			try {
				qemu_guest_agent_update_launch(isset($_POST['install']) ? 'install' : 'check', $_POST['check_id'] ?? '', $automatic);
			} catch (RuntimeException $error) {
				if (!$automatic || $error->getCode() !== 409) {
					throw $error;
				}
			}
			$redirect = !$automatic;
		}
	} catch (Throwable $error) {
		$input_errors[] = $error->getMessage();
	} finally {
		if (is_resource($lock)) {
			qemu_guest_agent_update_unlock($lock);
		}
	}
	if ($redirect) {
		header('Location: /qemu_guest_agent.php' . (isset($_POST['save']) ? '?saved=1' : ''));
		exit;
	}
}

if (isset($_GET['ajax']) || $automatic) {
	header('Content-Type: application/json');
	header('Cache-Control: no-store');
	try {
		$update = qemu_guest_agent_update_summary($can_update);
	} catch (Throwable $error) {
		$update = ['status' => 'error', 'message' => $error->getMessage(), 'busy' => false, 'can_update' => $can_update];
	}
	if ($input_errors) {
		$update['status'] = 'error';
		$update['message'] = implode(' ', $input_errors);
	}
	echo json_encode(['running' => is_service_running('qemu-ga'), 'update' => $update], JSON_INVALID_UTF8_SUBSTITUTE);
	exit;
}

$shortcut_section = 'qemu_guest_agent';
$pgtitle = [gettext('Services'), gettext('QEMU Guest Agent')];
include('head.inc');
if ($input_errors) {
	print_input_errors($input_errors);
} elseif (isset($_GET['saved'])) {
	print_info_box(gettext('QEMU Guest Agent settings saved.'), 'success');
}

$save = new Form_Button('save', 'Save');
$form = new Form($save);
$form->setAction('/qemu_guest_agent.php');
$form->setAttribute('id', 'qemu-service-form');
$service = new Form_Section('Service');
$service->addInput(new Form_Checkbox('enable', 'Enable', 'Start QEMU Guest Agent at boot.', qemu_guest_agent_enabled(), 'on'));
$service->addInput(new Form_StaticText('Service', '<span id="qemu-state">Checking…</span>'));
$service->addInput(new Form_StaticText('Version', '<span id="qemu-version">' .
	htmlspecialchars(qemu_guest_agent_version(), ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8') . '</span>'));
$service->addInput(new Form_StaticText('Status', '<span id="qemu-update-status" role="status" aria-live="polite">Checking…</span>'));
$form->add($service);
$form->addGlobal(new Form_Input('check_id', null, 'hidden', ''));
$install = new Form_Button('install', 'Update', null, 'fa-solid fa-download');
$install->setAttribute('id', 'qemu-update-install')->setAttribute('disabled', true)->addClass('btn-primary');
$form->addGlobal($install);
print $form;
?>

<script>
events.push(function () {
	let update = {};
	const form = document.getElementById('qemu-service-form');
	const install = document.getElementById('qemu-update-install');
	form.addEventListener('submit', function (event) {
		if (event.submitter === install && (install.disabled || !update.check_id)) {
			event.preventDefault();
		}
	});
	async function refresh(automatic = false) {
		try {
			const options = {cache: 'no-store', signal: AbortSignal.timeout(20000)};
			if (automatic) {
				options.method = 'POST';
				options.body = new FormData(form);
				options.body.set('autocheck', '1');
			}
			const response = await fetch('/qemu_guest_agent.php?ajax=1', options);
			if (!response.ok) throw new Error('Cannot read status.');
			const data = await response.json();
			update = data.update;
			const state = document.getElementById('qemu-state');
			state.textContent = data.running ? 'Running' : 'Stopped';
			state.className = data.running ? 'text-success' : 'text-danger';
			document.getElementById('qemu-version').textContent = update.installed || 'Unknown';
			document.getElementById('qemu-update-status').textContent = update.message;
			document.getElementById('check_id').value = update.check_id || '';
			install.disabled = !update.can_update || update.busy || update.status !== 'available';
			document.getElementById('save').disabled = update.busy;
		} catch (error) {
			document.getElementById('qemu-state').textContent = 'Status unavailable';
			document.getElementById('qemu-update-status').textContent = 'Cannot read status. Reload or sign in again.';
			install.disabled = true;
		} finally {
			setTimeout(refresh, update.busy ? 2000 : 5000);
		}
	}
	refresh(<?= $can_update ? 'true' : 'false' ?>);
});
</script>
<?php include('foot.inc'); ?>
