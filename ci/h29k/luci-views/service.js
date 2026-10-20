'use strict';
'require view';
'require rpc';
'require ui';

// Equivalent current rpcd interfaces for the original init-service ACL.
var callInitList = rpc.declare({
	object: 'rc',
	method: 'list',
	params: [ 'name' ],
	expect: { '': {} }
});

var callInitAction = rpc.declare({
	object: 'rc',
	method: 'init',
	params: [ 'name', 'action' ]
});

return view.extend({
	load: function() {
		return callInitList('modemd').then(function(result) {
			return { result: result };
		}).catch(function(error) {
			return { error: error };
		});
	},

	render: function(data) {
		var readonly = !L.hasViewPermission();
		var busy = false;
		var state = null;
		var controls = [];
		var running = E('span', {}, '未知');
		var enabled = E('span', {}, '未知');
		var message = E('p', { 'class': 'alert-message warning', 'style': 'display:none' });

		function updateButtons() {
			controls.forEach(function(button) {
				button.disabled = busy || readonly || !state;
			});
			refresh.disabled = busy;
		}

		function showError(text) {
			message.textContent = text;
			message.style.display = '';
		}

		function update(result) {
			state = result && result.modemd && typeof result.modemd === 'object'
				? result.modemd : null;
			running.textContent = state && typeof state.running === 'boolean'
				? (state.running ? '运行中' : '已停止') : '未知';
			enabled.textContent = state && typeof state.enabled === 'boolean'
				? (state.enabled ? '已启用' : '已禁用') : '未知';
			message.style.display = 'none';
			if (!state)
				showError('未找到 modemd 启动脚本，无法控制服务。');
			updateButtons();
		}

		function refreshStatus() {
			return callInitList('modemd').then(update);
		}

		function actionButton(label, action) {
			var button = E('button', {
				'class': 'cbi-button cbi-button-action',
				'type': 'button',
				'click': function() {
					if (readonly || busy || !state)
						return;
					busy = true;
					updateButtons();
					return callInitAction('modemd', action).then(function(result) {
						if (result !== 0)
							throw new Error('服务操作返回错误码：' + String(result));
						return refreshStatus();
					}).catch(function(error) {
						ui.addNotification(null, E('p', {},
							'服务操作或状态刷新失败：' + (error.message || String(error))), 'error');
					}).finally(function() {
						busy = false;
						updateButtons();
					});
				}
			}, label);
			controls.push(button);
			return button;
		}

		var refresh = E('button', {
			'class': 'cbi-button cbi-button-neutral',
			'type': 'button',
			'click': function() {
				if (busy)
					return;
				busy = true;
				updateButtons();
				return refreshStatus().catch(function(error) {
					showError('无法刷新服务状态：' + (error.message || String(error)));
				}).finally(function() {
					busy = false;
					updateButtons();
				});
			}
		}, '刷新状态');

		var buttons = E('div', { 'class': 'cbi-section-actions' }, [
			actionButton('启动', 'start'),
			actionButton('停止', 'stop'),
			actionButton('重启', 'restart'),
			actionButton('启用开机启动', 'enable'),
			actionButton('禁用开机启动', 'disable'),
			refresh
		]);

		update(data.result);
		if (data.error)
			showError('无法读取服务状态：' + (data.error.message || String(data.error)));

		return E('div', {}, [
			E('h2', {}, '设备中心服务'),
			E('p', {}, [ '运行状态：', running ]),
			E('p', {}, [ '开机启动：', enabled ]),
			message,
			readonly ? E('p', { 'class': 'alert-message notice' },
				'当前账号只有查看权限，无法更改服务状态。') : E('span'),
			buttons
		]);
	},

	handleSave: null,
	handleSaveApply: null,
	handleReset: null
});
