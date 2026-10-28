<?php

const QEMU_GUEST_AGENT_UPDATE_DIR = '/var/db/qemu-guest-agent-update';
const QEMU_GUEST_AGENT_REPOSITORY = 'https://github.com/r0bb10/pfSense-QEMU-Agent';
const QEMU_GUEST_AGENT_PACKAGE = 'pfSense-pkg-qemu-guest-agent';
const QEMU_GUEST_AGENT_PKG = '/usr/local/sbin/pkg';
const QEMU_GUEST_AGENT_UPDATE_PHASES = ['queued', 'checking', 'downloading', 'verifying', 'installing', 'restarting'];

function qemu_guest_agent_can_update() {
	$permissions = $_SESSION['page-match'] ?? [];
	return isAdminUID($_SESSION['Username'] ?? '') || in_array('*', $permissions, true) ||
		in_array('qemu_guest_agent-update', $permissions, true);
}

function qemu_guest_agent_update_directory() {
	if ((!is_dir(QEMU_GUEST_AGENT_UPDATE_DIR) && !mkdir(QEMU_GUEST_AGENT_UPDATE_DIR, 0700, true)) ||
		!chmod(QEMU_GUEST_AGENT_UPDATE_DIR, 0700)) {
		throw new RuntimeException('Cannot secure the update directory.');
	}
}

function qemu_guest_agent_update_state($state = null) {
	$file = QEMU_GUEST_AGENT_UPDATE_DIR . '/state.json';
	if ($state === null) {
		if (!is_file($file)) {
			return [];
		}
		$data = json_decode(file_get_contents($file), true, 512, JSON_THROW_ON_ERROR);
		if (!is_array($data)) {
			throw new RuntimeException('Update state is unreadable.');
		}
		return $data;
	}
	qemu_guest_agent_update_directory();
	$state['time'] = time();
	$temp = tempnam(QEMU_GUEST_AGENT_UPDATE_DIR, '.state-');
	if ($temp === false) {
		throw new RuntimeException('Cannot create update state.');
	}
	try {
		if (file_put_contents($temp, json_encode($state, JSON_THROW_ON_ERROR)) === false ||
			!chmod($temp, 0600) || !rename($temp, $file)) {
			throw new RuntimeException('Cannot save update state.');
		}
	} finally {
		if (is_file($temp)) {
			unlink($temp);
		}
	}
	return $state;
}

function qemu_guest_agent_update_lock($wait = false) {
	qemu_guest_agent_update_directory();
	$lock = fopen(QEMU_GUEST_AGENT_UPDATE_DIR . '/lock', 'ce');
	if ($lock === false) {
		throw new RuntimeException('Cannot open the QEMU Guest Agent operation lock.');
	}
	if (!flock($lock, LOCK_EX | ($wait ? 0 : LOCK_NB))) {
		fclose($lock);
		throw new RuntimeException('A QEMU Guest Agent operation is already running.', 409);
	}
	return $lock;
}

function qemu_guest_agent_update_unlock($lock) {
	flock($lock, LOCK_UN);
	fclose($lock);
}

function qemu_guest_agent_update_queued($state) {
	return ($state['status'] ?? '') === 'queued' && time() - ($state['time'] ?? 0) < 30;
}

function qemu_guest_agent_update_phase(&$state, $phase, $message) {
	$state['status'] = $phase;
	$state['message'] = $message;
	qemu_guest_agent_update_state($state);
}

function qemu_guest_agent_update_command($args, $timeout = 30) {
	$process = proc_open($args, [0 => ['file', '/dev/null', 'r'], 1 => ['pipe', 'w'], 2 => ['pipe', 'w']], $pipes);
	if (!is_resource($process)) {
		throw new RuntimeException('Cannot start package command.');
	}
	foreach ($pipes as $pipe) {
		stream_set_blocking($pipe, false);
	}
	$output = $errors = '';
	$started = microtime(true);
	try {
		do {
			$output = substr($output . stream_get_contents($pipes[1]), -65536);
			$errors = substr($errors . stream_get_contents($pipes[2]), -16384);
			$status = proc_get_status($process);
			if (!$status['running']) {
				$output = substr($output . stream_get_contents($pipes[1]), -65536);
				$errors = substr($errors . stream_get_contents($pipes[2]), -16384);
				break;
			}
			if ($timeout > 0 && microtime(true) - $started > $timeout) {
				proc_terminate($process, 9);
				throw new RuntimeException('Package query timed out. Retry when Package Manager is idle.');
			}
			usleep(100000);
		} while (true);
	} finally {
		foreach ($pipes as $pipe) {
			fclose($pipe);
		}
		proc_close($process);
	}
	if ($status['exitcode'] !== 0) {
		throw new RuntimeException('Package command failed: ' . trim(substr($output . $errors, -4000)));
	}
	return trim($output);
}

function qemu_guest_agent_update_installed() {
	return qemu_guest_agent_update_command([QEMU_GUEST_AGENT_PKG, 'query', '%v', QEMU_GUEST_AGENT_PACKAGE]);
}

function qemu_guest_agent_update_url($url) {
	$parts = is_string($url) ? parse_url($url) : false;
	$hosts = ['api.github.com', 'github.com', 'release-assets.githubusercontent.com',
		'objects.githubusercontent.com', 'github-releases.githubusercontent.com'];
	if (!$parts || ($parts['scheme'] ?? '') !== 'https' || !in_array($parts['host'] ?? '', $hosts, true) ||
		isset($parts['user']) || isset($parts['pass']) || isset($parts['fragment']) ||
		(isset($parts['port']) && $parts['port'] !== 443) || preg_match('/[\x00-\x20\x7f]/', $url)) {
		throw new RuntimeException('GitHub returned an unsupported download or redirect URL.');
	}
	return $url;
}

function qemu_guest_agent_update_http($url, $destination = null) {
	$deadline = microtime(true) + ($destination === null ? 30 : 300);
	for ($redirect = 0; $redirect <= 5; $redirect++) {
		qemu_guest_agent_update_url($url);
		$data = $location = '';
		$size = 0;
		$file = $destination === null ? null : fopen($destination, 'wb');
		if ($destination !== null && $file === false) {
			throw new RuntimeException('Cannot create downloaded package.');
		}
		$curl = curl_init($url);
		curl_setopt_array($curl, [
			CURLOPT_USERAGENT => 'pfSense-qemu-guest-agent-updater',
			CURLOPT_HTTPHEADER => ['Accept: application/vnd.github+json'],
			CURLOPT_CONNECTTIMEOUT => 10,
			CURLOPT_TIMEOUT => max(1, (int)ceil($deadline - microtime(true))),
			CURLOPT_FOLLOWLOCATION => false,
			CURLOPT_PROTOCOLS => CURLPROTO_HTTPS,
			CURLOPT_SSL_VERIFYPEER => true, CURLOPT_SSL_VERIFYHOST => 2,
			CURLOPT_HEADERFUNCTION => function ($curl, $header) use (&$location) {
				if (stripos($header, 'Location:') === 0) {
					$location = trim(substr($header, 9));
				}
				return strlen($header);
			},
			CURLOPT_WRITEFUNCTION => function ($curl, $chunk) use (&$data, &$size, $file) {
				$size += strlen($chunk);
				if ($size > ($file === null ? 2097152 : 134217728)) {
					return 0;
				}
				if ($file !== null) {
					return fwrite($file, $chunk);
				}
				$data .= $chunk;
				return strlen($chunk);
			},
		]);
		try {
			$ok = curl_exec($curl);
			$error = curl_error($curl);
			$code = curl_getinfo($curl, CURLINFO_RESPONSE_CODE);
		} finally {
			curl_close($curl);
			if ($file !== null) {
				fclose($file);
			}
		}
		if ($ok === false) {
			throw new RuntimeException('GitHub transfer failed: ' . $error);
		}
		if ($code === 200) {
			return $data;
		}
		if (in_array($code, [301, 302, 303, 307, 308], true) && $location !== '' && microtime(true) < $deadline) {
			$url = qemu_guest_agent_update_url($location);
			continue;
		}
		throw new RuntimeException('GitHub request failed (HTTP ' . $code . '). Retry later.');
	}
	throw new RuntimeException('Too many GitHub redirects.');
}

function qemu_guest_agent_update_parse_release($release) {
	// Accept older releases with FreeBSD revisions, but never expose those as QEMU versions.
	if (!is_array($release) || ($release['draft'] ?? true) !== false || ($release['prerelease'] ?? true) !== false ||
		!is_string($release['tag_name'] ?? null) || !preg_match('/^([0-9]+\.[0-9]+\.[0-9]+)(?:_[0-9]+)?$/D', $release['tag_name'], $match) ||
		!is_array($release['assets'] ?? null)) {
		throw new RuntimeException('No supported stable GitHub release available.');
	}
	$version = $match[1];
	$package_version = $release['tag_name'];
	$expected = QEMU_GUEST_AGENT_PACKAGE . '-' . $package_version . '.pkg';
	$assets = array_values(array_filter($release['assets'], fn($asset) => is_array($asset) && ($asset['name'] ?? '') === $expected));
	if (count($assets) !== 1) {
		throw new RuntimeException('Release must contain exactly one package matching its version.');
	}
	$asset = $assets[0];
	$url = QEMU_GUEST_AGENT_REPOSITORY . '/releases/download/' . $package_version . '/' . $expected;
	if (($asset['browser_download_url'] ?? '') !== $url || !is_string($asset['digest'] ?? null) ||
		!preg_match('/^sha256:([a-f0-9]{64})$/D', $asset['digest'], $digest) ||
		!is_int($asset['size'] ?? null) || $asset['size'] < 1 || $asset['size'] > 134217728) {
		throw new RuntimeException('Release package has an invalid URL, size or GitHub SHA-256 digest.');
	}
	return ['version' => $version, 'package_version' => $package_version, 'url' => $url, 'sha256' => $digest[1], 'size' => $asset['size']];
}

function qemu_guest_agent_update_launch($action, $check_id = '', $automatic = false) {
	if (!in_array($action, ['check', 'install'], true)) {
		throw new RuntimeException('Invalid update action.');
	}
	$lock = qemu_guest_agent_update_lock();
	try {
		try {
			$state = qemu_guest_agent_update_state();
		} catch (Throwable $error) {
			if ($action !== 'check') {
				throw $error;
			}
			$state = [];
		}
		if (qemu_guest_agent_update_queued($state)) {
			throw new RuntimeException('An update operation is already queued.', 409);
		}
		$installed = qemu_guest_agent_version();
		// Cache successful checks only. Reopening the page retries failed operations.
		if ($automatic && in_array($state['status'] ?? '', ['available', 'current'], true) &&
			($state['installed'] ?? '') === $installed && time() - ($state['checked_at'] ?? 0) < 900) {
			return;
		}
		if ($action === 'install' && (($state['status'] ?? '') !== 'available' || !is_string($check_id) || $check_id === '' ||
			!hash_equals($state['check_id'] ?? '', $check_id) || ($state['installed'] ?? '') !== $installed ||
			time() - ($state['checked_at'] ?? 0) > 86400 || !is_array($state['candidate'] ?? null))) {
			throw new RuntimeException('The checked update changed or expired. Check for updates again.');
		}
		$id = bin2hex(random_bytes(16));
		$request = ['operation_id' => $id, 'action' => $action, 'status' => 'queued',
			'message' => 'Starting update ' . ($action === 'check' ? 'check' : 'installation') . '...',
			'candidate' => $action === 'install' ? $state['candidate'] : null, 'installed' => $installed];
		qemu_guest_agent_update_state($request);
		exec('/usr/sbin/daemon -f /usr/local/bin/php -f /usr/local/pkg/qemu_guest_agent_update.php -- ' . escapeshellarg($id) . ' >/dev/null 2>&1', $output, $code);
		if ($code !== 0) {
			qemu_guest_agent_update_phase($request, 'error', 'Cannot start background updater.');
			throw new RuntimeException($request['message']);
		}
	} finally {
		qemu_guest_agent_update_unlock($lock);
	}
}

function qemu_guest_agent_update_verify_package($file, $release, $abi) {
	clearstatcache(true, $file);
	if (!is_file($file) || filesize($file) !== $release['size'] || !hash_equals($release['sha256'], hash_file('sha256', $file))) {
		throw new RuntimeException('Package size or SHA-256 verification failed. Nothing installed.');
	}
	$metadata = qemu_guest_agent_update_command([QEMU_GUEST_AGENT_PKG, 'query', '-F', $file, '%n|%v|%q']);
	if ($metadata !== QEMU_GUEST_AGENT_PACKAGE . '|' . $release['package_version'] . '|' . $abi) {
		throw new RuntimeException('Package name, version or FreeBSD ABI does not match. Nothing installed.');
	}
}

function qemu_guest_agent_update_probe_binary($file, $release) {
	// A matching major FreeBSD ABI does not guarantee pfSense's libc has all required symbols.
	$entries = explode("\n", qemu_guest_agent_update_command(['/usr/bin/tar', '-tf', $file]));
	$matches = array_values(array_filter($entries, fn($entry) => in_array($entry,
		['/usr/local/bin/qemu-ga', 'usr/local/bin/qemu-ga'], true)));
	if (count($matches) !== 1) {
		throw new RuntimeException('Package must contain exactly one QEMU Guest Agent binary. Nothing installed.');
	}
	// pfSense mounts /var/db noexec. Probe beside the installed executable instead.
	$probe = tempnam('/usr/local/bin', '.qemu-ga-probe-');
	if ($probe === false) {
		throw new RuntimeException('Cannot create binary compatibility probe. Nothing installed.');
	}
	try {
		if (dirname($probe) !== '/usr/local/bin') {
			throw new RuntimeException('Cannot create probe on the agent executable filesystem. Nothing installed.');
		}
		// Extract only the binary, never package scripts or arbitrary archive paths.
		$process = proc_open(['/usr/bin/tar', '-xOf', $file, $matches[0]],
			[0 => ['file', '/dev/null', 'r'], 1 => ['file', $probe, 'w'], 2 => ['pipe', 'w']], $pipes);
		if (!is_resource($process)) {
			throw new RuntimeException('Cannot extract binary compatibility probe. Nothing installed.');
		}
		stream_get_contents($pipes[2]);
		fclose($pipes[2]);
		if (proc_close($process) !== 0 || !chmod($probe, 0700)) {
			throw new RuntimeException('Cannot extract binary compatibility probe. Nothing installed.');
		}
		try {
			$version = qemu_guest_agent_update_command([$probe, '--version']);
		} catch (RuntimeException $error) {
			throw new RuntimeException('Downloaded agent cannot run on this pfSense: ' . $error->getMessage() .
				' Nothing installed; the current agent was not stopped.', 0, $error);
		}
		if ($version !== 'QEMU Guest Agent ' . $release['version']) {
			throw new RuntimeException('Downloaded binary version does not match the release. Nothing installed; the current agent was not stopped.');
		}
	} finally {
		unlink($probe);
	}
}

function qemu_guest_agent_update_install_package($file, $release, &$state) {
	qemu_guest_agent_update_phase($state, 'verifying', 'Verifying package integrity and binary compatibility...');
	$abi = qemu_guest_agent_update_command([QEMU_GUEST_AGENT_PKG, 'config', 'ABI']);
	qemu_guest_agent_update_verify_package($file, $release, $abi);
	qemu_guest_agent_update_probe_binary($file, $release);
	if (is_subsystem_dirty('pkg') || is_subsystem_dirty('packagelock')) {
		throw new RuntimeException('pfSense Package Manager is busy. Retry after it finishes.');
	}
	mark_subsystem_dirty('pkg');
	$stopped = false;
	$failure = null;
	try {
		if (qemu_guest_agent_version() !== $state['installed']) {
			throw new RuntimeException('Installed version changed. Check for updates again.');
		}
		qemu_guest_agent_update_phase($state, 'installing', 'Installing QEMU Guest Agent ' . $release['version'] . '...');
		$stopped = true;
		if (is_service_running('qemu-ga')) {
			stop_service('qemu-ga');
		}
		if (is_service_running('qemu-ga')) {
			throw new RuntimeException('Cannot stop the agent safely. Nothing installed.');
		}
		// Never time out an active package installation.
		qemu_guest_agent_update_command([QEMU_GUEST_AGENT_PKG, 'add', '-f', $file], 0);
		if (qemu_guest_agent_update_installed() !== $release['package_version'] || qemu_guest_agent_version() !== $release['version']) {
			throw new RuntimeException('Installed version does not match the selected release.');
		}
		$state['package_installed'] = true;
	} catch (Throwable $error) {
		$failure = $error;
	} finally {
		try {
			if ($stopped) {
				try {
					qemu_guest_agent_update_phase($state, 'restarting', 'Restoring service configuration and enabled state...');
				} catch (Throwable $error) {
					error_log('QEMU Guest Agent update diagnostics: ' . $error->getMessage());
				}
				qemu_guest_agent_sync();
				if (qemu_guest_agent_enabled() && !is_service_running('qemu-ga')) {
					throw new RuntimeException('The enabled agent did not start.');
				}
			}
		} catch (Throwable $error) {
			$failure = new RuntimeException(($failure ? $failure->getMessage() . ' ' : '') . 'Service recovery failed: ' . $error->getMessage());
		} finally {
			clear_subsystem_dirty('pkg');
		}
	}
	if ($failure) {
		throw $failure;
	}
}

function qemu_guest_agent_update_summary($can_update) {
	try {
		$lock = qemu_guest_agent_update_lock();
	} catch (RuntimeException $error) {
		if ($error->getCode() !== 409) {
			throw $error;
		}
		$lock = null;
	}
	try {
		$state = qemu_guest_agent_update_state();
		$busy = $lock === null || qemu_guest_agent_update_queued($state);
		if (!$busy && in_array($state['status'] ?? '', QEMU_GUEST_AGENT_UPDATE_PHASES, true)) {
			$state['status'] = 'error';
			$state['message'] = 'Update worker stopped unexpectedly. Installation may be incomplete; check before retrying.';
		}
		$installed = qemu_guest_agent_version();
		if (!$busy && ($state['status'] ?? '') === 'available' &&
			(($state['installed'] ?? '') !== $installed || time() - ($state['checked_at'] ?? 0) > 86400)) {
			$state['status'] = 'unchecked';
			$state['message'] = 'Check for updates again; the previous check expired or the installed version changed.';
		}
		return ['busy' => $busy, 'can_update' => $can_update, 'installed' => $installed,
			'status' => $state['status'] ?? 'unchecked', 'message' => $state['message'] ?? 'Not checked.',
			'latest' => $state['candidate']['version'] ?? '', 'check_id' => $state['check_id'] ?? ''];
	} finally {
		if (is_resource($lock)) {
			qemu_guest_agent_update_unlock($lock);
		}
	}
}
