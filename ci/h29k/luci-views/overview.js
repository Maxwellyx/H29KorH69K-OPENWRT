'use strict';
'require view';
'require fs';

// Read only the port in [server]. The destination host always comes from LuCI.
function listenPort(config) {
	var inServer = false;
	var lines = String(config || '').split(/\r?\n/);

	for (var i = 0; i < lines.length; i++) {
		var section = lines[i].match(/^\s*\[([^\]]+)\]\s*(?:#.*)?$/);
		if (section) {
			inServer = section[1].trim() === 'server';
			continue;
		}
		if (!inServer)
			continue;

		var listen = lines[i].match(/^\s*listen\s*=\s*(?:"([^"\r\n]*)"|'([^'\r\n]*)')\s*(?:#.*)?$/);
		if (!listen)
			continue;

		var port = (listen[1] != null ? listen[1] : listen[2]).match(/:(\d+)$/);
		var number = port ? Number(port[1]) : 0;
		return number >= 1 && number <= 65535 ? number : 9527;
	}

	return 9527;
}

return view.extend({
	load: function() {
		return fs.read('/etc/modemd/config.toml').then(function(config) {
			return { port: listenPort(config) };
		}).catch(function() {
			return { port: 9527, fallback: true };
		});
	},

	render: function(data) {
		var hostname = window.location.hostname;
		if (hostname.indexOf(':') !== -1 && hostname.charAt(0) !== '[')
			hostname = '[' + hostname + ']';

		var address = 'http://' + hostname + ':' + data.port + '/';
		var children = [
			E('h2', {}, '设备中心'),
			E('p', {}, '设备中心使用路由器账号单独登录，请在设备中心页面完成登录。'),
			E('p', {}, E('a', {
				'href': address,
				'target': '_blank',
				'rel': 'noopener noreferrer',
				'class': 'cbi-button cbi-button-action'
			}, '打开设备中心'))
		];

		if (data.fallback)
			children.push(E('p', { 'class': 'alert-message warning' },
				'无法读取设备中心配置，当前使用默认端口 9527。'));

		if (window.location.protocol === 'http:')
			children.push(E('iframe', {
				'src': address,
				'title': '设备中心',
				'style': 'width:100%;height:75vh;border:0',
				'referrerpolicy': 'no-referrer'
			}));
		else
			children.push(E('p', { 'class': 'alert-message notice' },
				'当前 LuCI 使用 HTTPS。设备中心使用 HTTP，请通过上方链接在新标签页打开。'));

		return E('div', {}, children);
	},

	handleSave: null,
	handleSaveApply: null,
	handleReset: null
});
