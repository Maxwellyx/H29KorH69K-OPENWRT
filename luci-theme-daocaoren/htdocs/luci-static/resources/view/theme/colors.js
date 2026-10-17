'use strict';
'require view';
'require uci';
'require ui';
'require dom';

/*
 *  主题外观（daocaoren 主题色设置）
 *  ------------------------------------------------------------------
 *  写的是主题自己的 uci 配置 /etc/config/daocaoren 的 global 段：
 *      primary / dark_primary   主题色（浅色/深色模式各一个，深色由浅色自动压暗得到）
 *      mode                     深浅模式：normal = 跟随系统、dark = 永远深色
 *      blur / blur_dark         毛玻璃模糊半径（px）
 *      transparency / *_dark    面板不透明度 0~1
 *  这些键被 themes/daocaoren/header.htm 读出来写进 :root 的 --primary /
 *  --dark-primary / --bar-bg / --blur-radius / --blur-opacity，所以改完刷新页面就生效。
 */

var PRESETS = [
	['#3b82f6', '标准蓝'], ['#2563eb', '深蓝'], ['#0ea5e9', '天蓝'], ['#06b6d4', '青色'],
	['#14b8a6', '蓝绿'], ['#10b981', '翡翠'], ['#22c55e', '绿色'], ['#84cc16', '黄绿'],
	['#eab308', '黄色'], ['#f59e0b', '琥珀'], ['#f97316', '橙色'], ['#ef4444', '红色'],
	['#f43f5e', '玫瑰'], ['#ec4899', '粉色'], ['#d946ef', '洋红'], ['#a855f7', '紫色'],
	['#8b5cf6', '紫罗兰'], ['#6366f1', '靛蓝'], ['#4f46e5', '深靛'], ['#64748b', '石板灰'],
	['#334155', '深灰'], ['#242424', '近黑']
];

var DEFAULT_COLOR = '#3b82f6';

function clamp(v, a, b) { return Math.min(b, Math.max(a, v)); }

function normHex(v) {
	v = String(v == null ? '' : v).trim().replace(/^#/, '');
	if (/^[0-9a-fA-F]{3}$/.test(v))
		v = v[0] + v[0] + v[1] + v[1] + v[2] + v[2];
	return /^[0-9a-fA-F]{6}$/.test(v) ? '#' + v.toLowerCase() : null;
}

function rgb(hex) {
	hex = normHex(hex) || DEFAULT_COLOR;
	return [parseInt(hex.substr(1, 2), 16), parseInt(hex.substr(3, 2), 16), parseInt(hex.substr(5, 2), 16)];
}

function toHex(c) {
	return '#' + c.map(function (v) { return ('0' + clamp(Math.round(v), 0, 255).toString(16)).slice(-2); }).join('');
}

/* 压暗（深色模式下用）—— 走 HSL 的 L 通道，比直接乘 RGB 更接近人眼观感 */
function darken(hex, f) {
	var c = rgb(hex).map(function (v) { return v / 255; });
	var mx = Math.max.apply(null, c), mn = Math.min.apply(null, c), l = (mx + mn) / 2, h = 0, s = 0;
	if (mx !== mn) {
		var d = mx - mn;
		s = l > .5 ? d / (2 - mx - mn) : d / (mx + mn);
		h = mx === c[0] ? (c[1] - c[2]) / d + (c[1] < c[2] ? 6 : 0)
			: mx === c[1] ? (c[2] - c[0]) / d + 2 : (c[0] - c[1]) / d + 4;
		h /= 6;
	}
	l = clamp(l * (f == null ? .84 : f), 0, 1);
	var q = l < .5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
	var h2r = function (t) {
		if (t < 0) t += 1; if (t > 1) t -= 1;
		if (t < 1 / 6) return p + (q - p) * 6 * t;
		if (t < 1 / 2) return q;
		if (t < 2 / 3) return p + (q - p) * (2 / 3 - t) * 6;
		return p;
	};
	return toHex([h2r(h + 1 / 3) * 255, h2r(h) * 255, h2r(h - 1 / 3) * 255]);
}

function card(title, note) {
	var cbi = E('div', { 'class': 'cbi-section' }, [
		E('h3', {}, title),
		note ? E('p', { 'class': 'cbi-section-descr' }, note) : ''
	]);
	Array.prototype.slice.call(arguments, 2).forEach(function (n) { if (n) cbi.appendChild(n); });
	return cbi;
}

/* rpcd 的 uci/apply 自带回滚计时器（luci.apply.rollback=90 秒），必须在限时内 confirm，
   否则配置会自己滚回去 —— 只 apply 不 confirm 就会表现得像“保存/切换没生效”。 */
function commit(uci) {
	return uci.save().then(function () {
		return uci.callApply(10, true);
	}).then(function (rv) {
		if (rv != 0)
			throw new Error('apply=' + rv);

		var deadline = Date.now() + 20000;
		var confirmLoop = function () {
			return L.resolveDefault(uci.callConfirm(), -1).then(function (crv) {
				if (crv == 0)
					return 0;
				if (Date.now() > deadline)
					throw new Error('confirm=' + crv);
				return new Promise(function (r) { window.setTimeout(r, 300); }).then(confirmLoop);
			});
		};
		return confirmLoop();
	});
}

return view.extend({
	load: function () {
		/* /etc/config/daocaoren 不存在时 uci.load 会抛 “uci/get 未找到资源”，
		   这里吞掉这个错：页面上按默认值显示，保存时会自动把配置文件建出来。 */
		return L.resolveDefault(uci.load('daocaoren'), null);
	},

	render: function () {
		var self = this;
		this.cur = normHex(uci.get('daocaoren', 'global', 'primary')) || DEFAULT_COLOR;
		this.swatches = [];
		this.preview = E('div', { 'class': 'cbi-value' });

		/* ---------- 当前色 / 预览 ---------- */
		var label = E('code', {}, this.cur);
		var darkLabel = E('code', {}, darken(this.cur));

		var paint = function (hex) {
			self.cur = hex;
			label.textContent = hex;
			darkLabel.textContent = darken(hex);
			var s = document.documentElement.style;
			s.setProperty('--primary', hex);
			s.setProperty('--dark-primary', darken(hex));
			s.setProperty('--bar-bg', hex);
			self.swatches.forEach(function (sw) {
				var on = normHex(sw.getAttribute('data-color')) === normHex(hex);
				sw.style.outline = on ? '3px solid var(--primary)' : '1px solid rgba(0,0,0,.15)';
				sw.style.outlineOffset = on ? '2px' : '0';
			});
			if (self.hex)
				self.hex.value = hex;
			return hex;
		};
		this.paint = paint;

		/* ---------- 预设色块 ---------- */
		var grid = E('div', { style: 'display:flex;flex-wrap:wrap;gap:10px;max-width:760px' });
		PRESETS.forEach(function (p) {
			var sw = E('button', {
				'type': 'button',
				'data-color': p[0],
				'title': p[1] + ' ' + p[0],
				'aria-label': p[1],
				'style': 'width:46px;height:46px;border-radius:10px;border:0;cursor:pointer;' +
					'background:' + p[0] + ';box-shadow:inset 0 0 0 1px rgba(0,0,0,.12)'
			});
			sw.addEventListener('click', function () { paint(p[0]); });
			self.swatches.push(sw);
			grid.appendChild(sw);
		});

		/* ---------- 自定义色值 ---------- */
		this.hex = E('input', {
			'type': 'text', 'class': 'cbi-input-text', 'placeholder': '#3b82f6',
			'style': 'width:130px', 'value': this.cur
		});
		var apply = E('button', { 'type': 'button', 'class': 'cbi-button cbi-button-neutral' }, '应用');
		apply.addEventListener('click', function () {
			var v = normHex(self.hex.value);
			if (!v) { ui.addNotification(null, E('p', {}, '颜色格式不对，要像 #3b82f6 这样'), 'error'); return; }
			paint(v);
		});

		paint(this.cur);

		/* ---------- 其它外观：深浅模式 / 毛玻璃 ---------- */
		this.mode = E('select', { 'class': 'cbi-input-select' });
		[['light', '强制浅色'], ['normal', '跟随系统'], ['dark', '强制深色']].forEach(function (o) {
			var opt = E('option', { 'value': o[0] }, o[1]);
			if ((uci.get('daocaoren', 'global', 'mode') || 'normal') === o[0])
				opt.setAttribute('selected', 'selected');
			self.mode.appendChild(opt);
		});
		this.blur = E('input', {
			'type': 'text', 'class': 'cbi-input-text', 'style': 'width:80px',
			'value': uci.get('daocaoren', 'global', 'blur') || '10'
		});

		var row = function (t, node) {
			return E('div', { 'class': 'cbi-value' }, [
				E('label', { 'class': 'cbi-value-title' }, t),
				E('div', { 'class': 'cbi-value-field' }, node)
			]);
		};

		/* ---------- 保存 ---------- */
		var save = E('button', {
			'type': 'button',
			'class': 'cbi-button cbi-button-apply'
		}, '保存并应用');
		save.addEventListener('click', function () {
			var v = normHex(self.cur) || DEFAULT_COLOR;
			var blur = parseInt(self.blur.value, 10);
			if (isNaN(blur) || blur < 0 || blur > 60) blur = 10;

			uci.set('daocaoren', 'global', 'primary', v);
			uci.set('daocaoren', 'global', 'dark_primary', darken(v));
			uci.set('daocaoren', 'global', 'mode', self.mode.value);
			uci.set('daocaoren', 'global', 'blur', String(blur));
			uci.set('daocaoren', 'global', 'blur_dark', String(blur));
			if (uci.get('daocaoren', 'global', 'transparency') == null)
				uci.set('daocaoren', 'global', 'transparency', '0.5');
			if (uci.get('daocaoren', 'global', 'transparency_dark') == null)
				uci.set('daocaoren', 'global', 'transparency_dark', '0.5');

			return commit(uci).then(function () {
				ui.addNotification(null, E('p', {}, '已保存：主题色 ' + v + '，页面即将刷新'), 'info');
				setTimeout(function () { location.reload(); }, 900);
			}).catch(function (e) {
				ui.addNotification(null, E('p', {}, '保存失败：' + e), 'danger');
			});
		});

		var reset = E('button', { 'type': 'button', 'class': 'cbi-button cbi-button-neutral' }, '恢复默认');
		reset.addEventListener('click', function () { paint(DEFAULT_COLOR); });

		return E('div', {}, [
			card('主题色', '改的是 --primary：侧栏选中项、按钮、链接、开关、进度条、登录页强调色都用它。点色块即时预览，保存后刷新页面生效。',
				grid,
				E('div', { 'class': 'cbi-value' }, [
					E('label', { 'class': 'cbi-value-title' }, '自定义'),
					E('div', { 'class': 'cbi-value-field' }, [this.hex, ' ', apply])
				]),
				E('div', { 'class': 'cbi-value' }, [
					E('label', { 'class': 'cbi-value-title' }, '当前'),
					E('div', { 'class': 'cbi-value-field' }, ['浅色 ', label, '　深色变体（自动） ', darkLabel])
				])),
			card('其它外观', '和主题色一起保存。深浅模式：light = 后台永远白底浅色、normal = 跟随系统、dark = 永远深色。',
				row('深浅模式', this.mode),
				row('毛玻璃模糊', [this.blur, ' px'])),
			E('div', { 'class': 'cbi-page-actions' }, [save, ' ', reset])
		]);
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
