/*!
 * daocaoren theme · 登录页粒子背景（自研 canvas：三层视差星点 + 偶发流星）
 * ---------------------------------------------------------------------------
 * 效果：深色底（#0f0f12）+ 一层缓慢漂移、明暗呼吸的星点，色调以白为主、少量青与紫，
 *      再叠两团极淡的光晕做纵深 —— 参考"AI 控制台"那类登录页的观感。
 *
 * 为什么自己写而不是抄别人的：
 *   · 这类星空本来就是几十行 canvas 的活，自己写才好控制性能与颜色，也没有版权/依赖问题；
 *   · 路由器/内网设备常常没外网，所以**不引任何外部资源**，全部在本地生成；
 *   · 只在登录页加载（见 themes/daocaoren/header_login.htm），管理页一条都不跑。
 *
 * 性能与礼貌：
 *   · 按 devicePixelRatio 渲染（高分屏不糊），但 dpr 上限 2，避免 4K 屏白烧 GPU；
 *   · 帧率封顶 ~30fps；页面不可见（切标签/锁屏）时暂停；窗口 resize 防抖；
 *   · 尊重 prefers-reduced-motion：只静态画一帧，不做动画。
 * ---------------------------------------------------------------------------
 */
(function () {
	'use strict';

	function boot() {
		var page = document.querySelector('.login-page');
		if (!page || document.getElementById('dc-login-bg')) return;

		var canvas = document.createElement('canvas');
		canvas.id = 'dc-login-bg';
		canvas.setAttribute('aria-hidden', 'true');
		page.insertBefore(canvas, page.firstChild);

		var ctx = canvas.getContext('2d');
		if (!ctx) { canvas.remove(); return; }

		var reduce = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;

		// —— 底色与颜色池：与登录页的青蓝辉光同一套语言 ——
		// 填**透明**：底色完全交给 CSS 那层雾（.login-page::before）。
		// ⚠️ 实测（把参考站 UI 全隐掉、只留画布截图）：它的画布本体是最暗 1% #010304、
		//    均值 #080e11 —— 也就是说**紫色不是画布画的**，是页面 CSS 的雾；
		//    之前我自己在画布上铺 rgba(5,7,14,0.60) 把 CSS 那层压掉了一大截，还得多加紫去补，反倒偏色。
		var BG = 'rgba(0,0,0,0)';
		// 星点：白为主 + 少量青/紫
		var WHITE = '235,245,255', CYAN = '34,211,238', VIOLET = '167,139,250';
		var PALETTE = [WHITE, WHITE, WHITE, WHITE, '110,200,245', VIOLET, '175,175,255'];

		// 四层深度：layer 0 = 最远（小/暗/慢），3 = 最近（大/亮/快）⇒ 视差纵深。
		// ⚠️ 用户反馈"很假"的真正原因就是**所有星点一样大、一样亮、同步漂** ⇒ 看着像一张平面贴图。
		//    纵深靠三件事：密度分层 + 尺寸/亮度随深度缩放 + **转速差**（近的快）。
		var LAYERS = [
			{ density: 1 / 2400, rMin: 0.26, rMax: 0.70, aMin: 0.24, aMax: 0.55, speed: 0.018 },
			{ density: 1 / 3200, rMin: 0.40, rMax: 1.05, aMin: 0.34, aMax: 0.75, speed: 0.040 },
			{ density: 1 / 5200, rMin: 0.65, rMax: 1.55, aMin: 0.45, aMax: 0.90, speed: 0.072 },
			{ density: 1 / 11000, rMin: 1.10, rMax: 2.50, aMin: 0.55, aMax: 1.00, speed: 0.115 }
		];
		var MAX_STARS = 1700;

		// —— 可选效果开关（想关掉某一层，把对应值改成 false 即可）——
		var EFFECTS = { stars: true, meteors: true, meteorBurst: true, hexGrid: false, galaxy: true, sparkle: true };
		// 六边形网格：间距 / 线透明度 / 漂移速度 / 高亮节点数
		var HEX = { step: 54, alpha: 0.075, drift: 0.05, hot: 16 };
		// 螺旋星河：参数是**从参考站截图量出来的**，不是调出来的（量法：`node D:\OPENWRT\_dev\galmeas.js <png>`）
		//   实测：盘面轴比 0.58、主轴 +32°、质心≈屏幕中心、亮部 #d1d4df（近白，B/R≈1.06）、
		//        中部 #5f506f（紫，G/R≈0.84 ⇒ 红>绿）、暗部 #332d56（靖紫，B/R≈1.69）
		//   ⇒ ① 粒子本体是**白**的，不是青；② 尘埃是**靖紫**；③ 盘面比我之前斜得多；④ 核要在屏幕中心
		var GALAXY = {
			arms: 2,           // 主旋臂条数
			twist: 12.0,       // 缠绕圈数：用户要求"多转几圈"（12 rad ≈ 1.9 圈；4.6 rad 只有 0.73 圈 = 一半）
			// ⚠️ 两条旋臂的**半径倍数**：不同的话才会"一大一小"（用户：上下 2 个圈要不一样大）。
			//    默认 180° 对称的两条臂看上去大小完全一样，假。
			//    按盘面角 0 = 屏幕右下（约 4~5 点方向）、π = 左上（约 10~11 点）。
			armScale: [1.00, 0.62],
			armTwist: [1.00, 1.30],   // 小的那条卷得更紧（同一套数放进去才能对上）
			tilt: 1.20,        // 盘面主轴方向(rad)；实测参考站主轴 +60.9°、本版量出 +41° ⇒ 再补 8° 校准
			squash: 0.83,      // 压扁比＝实测轴比 0.83（之前我按整页截图算成 0.58，那是被 CSS 雾带偏的）
			// ⚠️ spin 的**符号很重要**：负值 = 屏幕上的**逆时针(CCW)**；它必须和 twist 反号才算"拖尾臂"。
			spin: -0.030,      // 自转 rad/s（负 = 逆时针）
			// 差速：**越靠近中心转得越快**（中心是引力源，内圈角速度高）。
			// ⚠️ 不能按"随机远近"来——那是噪点不是转动（用户：常理在哪里）。
			// ⚠️ 不能给大：盘面（尘埃/光带）是"烘"出来的贴图、只能刚性旋转，亮粒切太多会与它错开。
			//    0.18 ≈ 内圈快 18%；开一分钟也才错开 ~10°（光带很淡，看不出来）。
			shear: 0.18,       // 内圈相对外圈快 18%（中心是引力源 ⇒ 内圈角速度高）
			beads: 700,        // 每条旋臂上的团簇数
			// ⚠️ per 和 beads 一起决定 walk() 的**产能上限**（beads×arms×per）。
			//    容量不够时 walk() 会静默提前退出：我上一版 beads=120/per=11 ⇒ 产能只有 ~1900 颗，
			//    而要 8928 颗尘埃 ⇒ 尘埃层几乎没生成，画面只剩"稀疏的大珠子串"。
			per: 34,           // 每个团簇内的粒子数上限（产能 ≈ arms×beads×per）
			count: 5200,       // 逐帧绘制的**亮粒**上限
			dustBake: 60000    // 预渲染进离屏贴图的**尘埃**上限（只 blit 一次，便宜；粒要小、数量要多）
		};

		var dpr = Math.min(window.devicePixelRatio || 1, 2);
		var W = 0, H = 0, stars = [], bursts = [], shots = [], shotAcc = 0, shotNext = 4000;
		var grid = null, gridW = 0, gridH = 0, gridOff = 0, hotNodes = [];
		var gal = [], haze = [], dust = null, galCx = 0, galCy = 0, galR = 0, galMul = 1, flares = [], sparkleAcc = 0, sparkleNext = 3500;

		function rnd(a, b) { return a + Math.random() * (b - a); }

		// —— 光斑 sprite：一次性预渲染的"柔光点" ——
		// 为什么不用逐粒子 createRadialGradient：那玩意儿每帧每粒子都要新建一个对象，
		// 几千粒子必卡；预渲染一张贴图 + drawImage 缩放，才是 2D canvas 画大量光点最便宜的路。
		var SPRITES = null;
		// 调色板（把参考站 UI 全隐掉、只量它的画布本体得到的，别凭感觉改）：
		//   亮粒 lum>110 → #a1b3b8（B/R≈1.14、G/R≈1.11）—— **灰白偏青、饱和度极低**
		//   中景 70~110 → #4a5c62
		//   暗部 45~70  → #2e3c41（R<G<B，灰青）
		// ⭐ 关键：真实河系照片就是"低饱和"，我之前用的高饱和青/紫就是"假"的根源。
		var TINTS = [
			'232,240,244',   // 0 近白（冷青）
			'200,214,220',   // 1 冷白
			'150,175,182',   // 2 灰青（中景）
			'118,145,152',   // 3 灰青（暗）
			'92,118,125',    // 4 深灰青
			'74,98,105'      // 5 最暗（尘埃）
		];
		var DUST_TINT = [3, 4, 5];          // 尘埃：灰青（实测 R<G<B）
		var BODY_TINT = [0, 0, 0, 1, 2];    // 亮粒本体：白为主
		var MID_TINT = [1, 2, 2, 3];        // 旋臂中景

		// ⚠️ flat=true = **没有白芯**。柔光斑缩小到 2~3px 后可见区域只剩中心那抹白，
		//    颜色根本展现不出来（我上一版改了尘埃颜色却没变化，就是这个原因）⇒ 尘埃专用 flat 贴图。
		function makeGlow(size, rgb, sparkle, flat) {
			var c = document.createElement('canvas');
			c.width = c.height = size;
			var s = c.getContext('2d');
			if (!s) return c;
			var h = size / 2;
			// 芯是白的：加法混合叠起来才有参考图那种"过曝"的亮心（flat 贴图不要白芯）
			var g = s.createRadialGradient(h, h, 0, h, h, h);
			g.addColorStop(0, flat ? 'rgba(' + rgb + ',1)' : 'rgba(255,255,255,1)');
			g.addColorStop(0.16, 'rgba(' + rgb + ',0.92)');
			g.addColorStop(0.42, 'rgba(' + rgb + ',0.30)');
			g.addColorStop(1, 'rgba(' + rgb + ',0)');
			s.fillStyle = g;
			s.beginPath(); s.arc(h, h, h, 0, 6.2832); s.fill();
			if (sparkle) {
				// 四芒星：参考图里每颗亮星都带的两道细十字光
				var t1 = Math.max(1, size * 0.022);
				var lg1 = s.createLinearGradient(0, h, size, h);
				lg1.addColorStop(0, 'rgba(' + rgb + ',0)');
				lg1.addColorStop(0.5, 'rgba(255,255,255,0.90)');
				lg1.addColorStop(1, 'rgba(' + rgb + ',0)');
				s.fillStyle = lg1;
				s.fillRect(0, h - t1 / 2, size, t1);
				var lg2 = s.createLinearGradient(h, 0, h, size);
				lg2.addColorStop(0, 'rgba(' + rgb + ',0)');
				lg2.addColorStop(0.5, 'rgba(255,255,255,0.72)');
				lg2.addColorStop(1, 'rgba(' + rgb + ',0)');
				s.fillStyle = lg2;
				s.fillRect(h - t1 / 2, 0, t1, size);
			}
			return c;
		}

		function buildSprites() {
			if (SPRITES) return;
			SPRITES = { dust: [], dot: [], big: [], star: [] };
			for (var i = 0; i < TINTS.length; i++) {
				SPRITES.dust.push(makeGlow(14, TINTS[i], false, true));    // 纯色，微观粒子/雾用
				SPRITES.dot.push(makeGlow(28, TINTS[i], false, false));
				SPRITES.big.push(makeGlow(64, TINTS[i], false, false));
				SPRITES.star.push(makeGlow(72, TINTS[i], true, false));
			}
		}

		// （旧版有个 tintFor() 按半径随机挑颜色；现在改成三个显式色表 DUST/BODY/MID_TINT，
		//   因为实测发现"亮粒是白的、尘埃是靖紫的"，不该混在一个按半径的随机里）

		function build() {
			stars = [];
			var total = 0;
			for (var li = 0; li < LAYERS.length; li++) {
				var L = LAYERS[li];
				var n = Math.round(W * H * L.density);
				for (var i = 0; i < n && total < MAX_STARS; i++, total++) {
					var big = Math.random() < 0.09;              // 少量"亮星"（带柔光）
					stars.push({
						l: li,
						x: Math.random() * W,
						y: Math.random() * H,
						r: big ? rnd(L.rMax, L.rMax * 1.6) : rnd(L.rMin, L.rMax),
						a: rnd(L.aMin, L.aMax),
						tw: rnd(0.4, 1.7),                        // 呼吸速度
						ph: Math.random() * Math.PI * 2,          // 呼吸相位
						vx: rnd(-1, 1) * L.speed,
						vy: rnd(-1, 1) * L.speed,
						c: PALETTE[(Math.random() * PALETTE.length) | 0],
						glow: big || (li === 2 && Math.random() < 0.3)
					});
				}
			}
		}

		// 偶发流星：每 4~9 秒一条，斜向划过并淡出（给静态页面一点"活气"）
		function spawnShot() {
			var fromLeft = Math.random() < 0.5;
			var speed = rnd(5.5, 9.5);                    // px/帧（30fps 下很快）
			shots.push({
				x: fromLeft ? rnd(-0.1, 0.5) * W : rnd(0.5, 1.1) * W,
				y: rnd(-0.05, 0.45) * H,
				vx: fromLeft ? speed : -speed,
				vy: speed * rnd(0.28, 0.5),
				len: rnd(90, 210),
				w: rnd(2.2, 4.2),                     // 头部半宽（尾迹从它往尾尖收细）
				life: 1
			});
		}

		// 螺旋星河：分两层，密度与性能两头都要
		//   · 尘埃层：占绝大多数粒子，**预渲染**进一张离屏贴图 ⇒ 每帧只一次 drawImage。
		//     2D canvas 里上万粒子逐帧画是画不动的，但"烘一次"完全没问题。
		//   · 亮粒层：~1.5k 个逐帧画（**圆的**柔光斑，不跟着压扁 —— 和 three.js 的 gl_PointSize 同理），
		//     自转与明暗呼吸都在这一层。
		//   ⚠️ 两层必须共用同一个变换：screen = T(cx,cy)·S(1,squash)·R(tilt+rot)·(盘面坐标)。
		//      尘埃贴图是"脸对脸"烘的，绘制时才压扁 + 旋转；亮粒直接套同一个公式。
		function buildGalaxy() {
			gal = []; haze = []; dust = null;
			if (!EFFECTS.galaxy || W === 0 || H === 0) return;
			buildSprites();

			// 位置：实测参考站的质心就在**屏幕中心**（约 47% 宽 / 43% 高）—— 别再把核推到边上；
			// 登录卡是玻璃的，压在河系上正好透出下面的光
			var narrow = W < 820;
			galMul = narrow ? 0.82 : 1;
			galCx = W * 0.50;
			galCy = narrow ? H * 0.42 : H * 0.52;
			// ⚠️ 尺度是重的：实测参考站亮部 sigma 只有 111px（截图像素）⇒ 亮盘半径约 250px。
			//    我上一版给了 1024px，整个盘面大半被屏幕裁掉，量出来的"椭圆"其实是屏幕形状。
			galR = Math.min(W * 0.30, H * 0.46) * (narrow ? 0.78 : 1);

			var TAU = Math.PI * 2, made = 0;
			// 旋臂分布：beads 个"珠子"串在旋臂上，每个珠子再撒几颗 —— 尘埃和亮粒共用
			// ⚠️ 循环顺序很重要：**以珠子为外层、旋臂为内层**。反过来（先遍历旋臂）的话，
			//    want 一满就在第一条旋臂里返回，第二条旋臂颗粒无收 —— 画面上就是"只有一条 C 形臂"。
			function walk(want, cb) {
				var perBead = Math.max(1, Math.ceil(want / (GALAXY.arms * GALAXY.beads)));
				for (var b = 0; b < GALAXY.beads && made < want; b++) {
					// ⚠️ 指数 >1 = **内密外疏**。用 0.6 的话（x^0.6 > x）会把粒子往外推，
					//    中心就成了一个大空洞、整个星河看着像个戒指。
					// ⚠️ 另外留出内圈 10%：旋臂直接伸到圆心会叠成一根过曝的白色"回形针"，
					//    中心交给核球（它才是该亮的地方）。
					var t = 0.16 + 0.84 * Math.pow((b + Math.random()) / GALAXY.beads, 1.50);
					var baseA = t * GALAXY.twist + rnd(-1, 1) * (0.012 + 0.075 * t);
					var baseR = t * galR + rnd(-1, 1) * (0.02 + 0.07 * t) * galR;
					var spA = 0.012 + 0.055 * t, spR = (0.010 + 0.050 * t) * galR;

					for (var a = 0; a < GALAXY.arms && made < want; a++) {
						// 每条臂自己的半径倍数/缠绕系数（与上面光带、外面绘制三者共用同一套）
						var scA = GALAXY.armScale[a % GALAXY.armScale.length];
						var twA = GALAXY.armTwist[a % GALAXY.armTwist.length];
						var bAng = a * TAU / GALAXY.arms + baseA * twA;
						var bRad = baseR * scA;
						var m = 1 + (Math.random() * perBead | 0);
						for (var k = 0; k < m && made < want; k++) {
							cb(bAng + rnd(-1, 1) * spA, bRad + rnd(-1, 1) * spR, t);
							made++;
						}
					}
				}
			}

			// —— ① 尘埃贴图（脸对脸的盘面坐标，半径 galR 的方图，中心=盘心）——
			var res = Math.min(dpr, 1.5);
			var side = Math.ceil(galR * 2 * res);
			dust = document.createElement('canvas');
			dust.width = side; dust.height = side;
			var dc = dust.getContext('2d');
			if (dc) {
				dc.setTransform(res, 0, 0, res, 0, 0);
				dc.globalCompositeOperation = 'lighter';

				// —— ⭐ 旋臂"光带"：沿螺旋中心线铺一串柔光斑 ——
				// 只靠点状粒子永远会有空隙，画面上就是"点串成的链"（假）。真实河系照片是
				// **连续雾 + 点缀**：所以先用低透明度的柔光把旋臂连成光带，再把粒子撒上去。
				for (var a2 = 0; a2 < GALAXY.arms; a2++) {
					var steps = 260;
					for (var s2 = 0; s2 < steps; s2++) {
						var tt = Math.pow(s2 / steps, 0.66);
						var aa2 = a2 * TAU / GALAXY.arms + tt * GALAXY.twist;
						// ⚠️ 宽度**内宽外细**（往外 t↑ 越窄），并加横向抖动：
						//    完全平滑 + 外宽内细的话，外圈会变成一条宽光带（用户：尾巴要宽、往下越细）
						var rr2 = tt * galR + rnd(-1, 1) * galR * 0.045 * tt;
						var px2 = galR + Math.cos(aa2) * rr2 + rnd(-1, 1) * galR * 0.02;
						var py2 = galR + Math.sin(aa2) * rr2 + rnd(-1, 1) * galR * 0.02;
						var rad2 = galR * (0.030 + 0.105 * tt);
						var gg = dc.createRadialGradient(px2, py2, 0, px2, py2, rad2);
						gg.addColorStop(0, 'rgba(122,150,160,' + (0.045 * (1 - 0.75 * tt)).toFixed(4) + ')');
						gg.addColorStop(1, 'rgba(122,150,160,0)');
						dc.fillStyle = gg;
						dc.beginPath(); dc.arc(px2, py2, rad2, 0, 6.2832); dc.fill();
					}
				}
				// —— 核球雾：中心一团柔光（参考站中心是一片连续的亮，不是一堆点）——
				var bgr = galR * 0.18;
				var bg = dc.createRadialGradient(galR, galR, 0, galR, galR, bgr);
				bg.addColorStop(0, 'rgba(205,225,232,0.14)');
				bg.addColorStop(0.35, 'rgba(150,175,185,0.06)');
				bg.addColorStop(1, 'rgba(120,145,155,0)');
				dc.fillStyle = bg;
				dc.beginPath(); dc.arc(galR, galR, bgr, 0, 6.2832); dc.fill();

				made = 0;
				walk(Math.min(GALAXY.dustBake, Math.round(galR * 90)), function (ang, rr, t) {
					var sp = SPRITES.dust[DUST_TINT[(Math.random() * DUST_TINT.length) | 0]];
					var zi = Math.random();                       // 深度：0=远 1=近
					var sz = rnd(0.9, 2.8) * (1 - 0.30 * t) * (0.50 + 0.90 * zi);
					dc.globalAlpha = rnd(0.18, 0.46) * (1 - 0.40 * t) * (0.40 + 0.75 * zi);
					dc.drawImage(sp, galR + Math.cos(ang) * rr - sz, galR + Math.sin(ang) * rr - sz, sz * 2, sz * 2);
				});
			}

			// —— ② 亮粒（逐帧画）——
			made = 0;
			var bn = Math.max(400, Math.min(GALAXY.count, Math.round(W * H / 1000)));
			walk(bn, function (ang, rr, t) {
				var r = Math.random();
				var ti = r < 0.60 ? BODY_TINT[(Math.random() * BODY_TINT.length) | 0]
					: MID_TINT[(Math.random() * MID_TINT.length) | 0];
				// —— 纵深（用户要的"由近到远"）——
				//   z=0 远景：小、暗； z=1 近景：大、亮；近侧还有一点点透视放大
				//   （差速不跟着 z，而是按**半径**算，见下面的 dw）
				var z = Math.random();
				var big = z > 0.90;                            // 大光斑 = 近景（而不是另外随机）
				var persp = 0.88 + 0.22 * z;
				var rn = Math.min(1, rr / galR);
				gal.push({
					rx: Math.cos(ang) * rr * persp, ry: Math.sin(ang) * rr * persp,
					sp: big ? SPRITES.big[ti] : SPRITES.dot[ti],
					sz: (big ? rnd(5, 11) : rnd(1.4, 4.6)) * (0.55 + 0.85 * z),
					// ⚠️ 亮度剖面：中心要**凹陷**（中心由核球光晕主导，内圈旋臂别抢）。
					//    直接 (1-0.45t) 会让内圈最亮 + 核球叠加 ⇒ 过曝成一根白色"回形针"。
					a: (big ? rnd(0.35, 0.55) : rnd(0.35, 0.85)) * (1 - 0.40 * t) * (0.40 + 0.70 * z)
						* Math.min(1, 0.28 + 2.2 * t),
					dw: GALAXY.spin * GALAXY.shear * (1 - rn),   // 内圈更快（引力）
					ph: Math.random() * TAU,
					tw: Math.random() < 0.10                    // 只让一小部分粒子闪，否则会"噪"
				});
			});

			// —— ③ 核球：一撮白粒（核球光晕另外用渐变画）——
			var coreN = Math.round(bn * 0.08), coreR = galR * 0.09;
			for (var c = 0; c < coreN; c++) {
				var ca = Math.random() * TAU, cr = Math.pow(Math.random(), 0.6) * coreR;
				var star = Math.random() < 0.05;
				gal.push({
					rx: Math.cos(ca) * cr, ry: Math.sin(ca) * cr,
					sp: SPRITES[star ? 'star' : 'dot'][0],
					sz: star ? rnd(5, 11) : rnd(1.2, 4),
					a: rnd(0.10, 0.28),
					ph: Math.random() * TAU,
					tw: Math.random() < 0.16
				});
			}

			// —— ④ 外场零星亮星：给盘面一圈若有若无的边界（用户："外面的星要多"）——
			var haloN = Math.round(bn * 0.16);
			for (var e = 0; e < haloN; e++) {
				var ha2 = Math.random() * TAU, hr2 = galR * rnd(0.75, 1.45);
				var z2 = Math.random();
				var rn2 = Math.min(1.4, hr2 / galR);
				gal.push({
					rx: Math.cos(ha2) * hr2, ry: Math.sin(ha2) * hr2,
					sp: SPRITES.dot[Math.random() < 0.35 ? 1 : 4],
					sz: rnd(1.2, 3.6) * (0.55 + 0.85 * z2),
					a: rnd(0.06, 0.20) * (0.40 + 0.70 * z2),
					dw: GALAXY.spin * GALAXY.shear * Math.max(0, 1 - rn2),
					ph: Math.random() * TAU,
					tw: Math.random() < 0.35
				});
			}

			// —— ⑤ 星云薄雾：3 团极淡的灰青（参考站画布里的雾就是灰青，不是紫）——
			var hazeCols = ['110,140,150', '90,120,130', '130,160,170'];
			for (var h = 0; h < 3; h++) {
				var ha = h * TAU / 3 + rnd(-0.6, 0.6), hr = rnd(0.20, 0.55);
				haze.push({
					rx: Math.cos(ha) * hr, ry: Math.sin(ha) * hr,
					r: rnd(0.30, 0.55), a: rnd(0.020, 0.040), c: hazeCols[h]
				});
			}
		}

		function drawGalaxy(t, staticFrame) {
			if (!gal.length || !SPRITES) return;
			// 盘面坐标 → 屏幕：**先在盘面里压扁、再整体旋转**（screen = T·R·S·盘面坐标）。
			// ⚠️ 顺序很关键：如果反过来（先在屏幕里压扁），椭圆的主轴就永远是水平的
			//    （实测我这版主轴只有 +2°，而参考站是 +60.7°，一看就不对）。
			var R = GALAXY.tilt + (staticFrame ? 0 : t * GALAXY.spin);
			var cosR = Math.cos(R), sinR = Math.sin(R), sq = GALAXY.squash;

			// —— 星云薄雾：3 团极淡的紫，跟着盘面一起转 ——
			for (var h = 0; h < haze.length; h++) {
				var hz = haze[h];
				var hx = galCx + (hz.rx * cosR - hz.ry * sq * sinR) * galR;
				var hy = galCy + (hz.rx * sinR + hz.ry * sq * cosR) * galR;
				var hr = hz.r * galR;
				var hg = ctx.createRadialGradient(hx, hy, 0, hx, hy, hr);
				hg.addColorStop(0, 'rgba(' + hz.c + ',' + (hz.a * galMul).toFixed(3) + ')');
				hg.addColorStop(1, 'rgba(' + hz.c + ',0)');
				ctx.fillStyle = hg;
				ctx.beginPath(); ctx.arc(hx, hy, hr, 0, 6.2832); ctx.fill();
			}

			// —— **加法混合**：重叠处自然叠到白，就是参考图核球那种"过曝"感；
			//    用 source-over 永远做不出这种亮度（会把下面的粒子盖掉）——
			ctx.globalCompositeOperation = 'lighter';

			// —— ① 尘埃贴图：一次 blit。transform = translate·scale(1,sq)·rotate(R)，
			//    和下面亮粒的公式**完全一致**，两层才对得齐 ——
			if (dust) {
				ctx.save();
				ctx.translate(galCx, galCy);
				ctx.rotate(R);          // 先转（和上面同一个 R）
				ctx.scale(1, sq);       // 再在盘面里压扁
				ctx.globalAlpha = 0.9 * galMul;
				ctx.drawImage(dust, -galR, -galR, galR * 2, galR * 2);
				ctx.restore();
			}

			// —— ② 亮粒（圆的柔光斑，不跟压扁 ⇒ 和 three.js 的 gl_PointSize 同理）——
			// p.dw 是**差速**（近的转得快、远的慢）：深度感主要来自这里。
			// 逐粒子算 sin/cos：5k 粒子 ≈ 10k 次调用/帧，在这个量级完全划算（也是用户要的"由近到远"）。
			for (var i = 0; i < gal.length; i++) {
				var p = gal[i];
				var cR = cosR, sR = sinR;
				if (p.dw && !staticFrame) {
					var Rp = R + t * p.dw;
					cR = Math.cos(Rp); sR = Math.sin(Rp);
				}
				var x = galCx + p.rx * cR - p.ry * sq * sR;
				var y = galCy + p.rx * sR + p.ry * sq * cR;
				if (x < -40 || x > W + 40 || y < -40 || y > H + 40) continue;   // 屏幕外直接跳过
				var a = p.a;
				if (p.tw && !staticFrame) a *= 0.62 + 0.38 * Math.sin(t * 1.5 + p.ph);
				a *= galMul;
				ctx.globalAlpha = a > 1 ? 1 : a;
				ctx.drawImage(p.sp, x - p.sz, y - p.sz, p.sz * 2, p.sz * 2);
			}
			ctx.globalAlpha = 1;

			// —— ③ 核球：小、收敛。实测参考站整屏 lum>110 的像素只有 ~3.5k，
			//    说明它的核球很“小气”——比之前那版一大团白光真实得多 ——
			var coreR = Math.max(16, galR * 0.095);
			var g3 = ctx.createRadialGradient(galCx, galCy, 0, galCx, galCy, coreR * 3.2);
			g3.addColorStop(0, 'rgba(100,130,140,' + (0.10 * galMul).toFixed(3) + ')');
			g3.addColorStop(0.40, 'rgba(70,95,105,' + (0.04 * galMul).toFixed(3) + ')');
			g3.addColorStop(1, 'rgba(70,95,105,0)');
			ctx.fillStyle = g3;
			ctx.beginPath(); ctx.arc(galCx, galCy, coreR * 3.2, 0, 6.2832); ctx.fill();

			var g2 = ctx.createRadialGradient(galCx, galCy, 0, galCx, galCy, coreR);
			g2.addColorStop(0, 'rgba(255,255,255,' + (0.62 * galMul).toFixed(3) + ')');
			g2.addColorStop(0.25, 'rgba(214,226,232,' + (0.26 * galMul).toFixed(3) + ')');
			g2.addColorStop(0.65, 'rgba(120,145,155,' + (0.09 * galMul).toFixed(3) + ')');
			g2.addColorStop(1, 'rgba(95,118,126,0)');
			ctx.fillStyle = g2;
			ctx.beginPath(); ctx.arc(galCx, galCy, coreR, 0, 6.2832); ctx.fill();

			var g1 = ctx.createRadialGradient(galCx, galCy, 0, galCx, galCy, coreR * 0.32);
			g1.addColorStop(0, 'rgba(255,255,255,' + (0.70 * galMul).toFixed(3) + ')');
			g1.addColorStop(0.6, 'rgba(238,242,250,' + (0.22 * galMul).toFixed(3) + ')');
			g1.addColorStop(1, 'rgba(255,255,255,0)');
			ctx.fillStyle = g1;
			ctx.beginPath(); ctx.arc(galCx, galCy, coreR * 0.32, 0, 6.2832); ctx.fill();
			ctx.globalCompositeOperation = 'source-over';

			// —— 核球的四芒星光芒：跟着盘面一起转（对齐旋臂起始方向），很淡但很关键 ——
			var pulse = staticFrame ? 0.40 : 0.30 + 0.12 * Math.sin(t * 1.1);
			pulse *= galMul;
			ctx.lineCap = 'round';
			for (var k = 0; k < 2; k++) {
				var aa = R + k * Math.PI / 2;
				var L = coreR * (k === 0 ? 2.6 : 1.35);
				var ex = galCx + Math.cos(aa) * L, ey = galCy + Math.sin(aa) * L;
				var ex2 = galCx - Math.cos(aa) * L * 0.7, ey2 = galCy - Math.sin(aa) * L * 0.7;
				var lg = ctx.createLinearGradient(ex2, ey2, ex, ey);
				lg.addColorStop(0, 'rgba(34,211,238,0)');
				lg.addColorStop(0.45, 'rgba(200,245,255,' + (pulse * 0.55).toFixed(3) + ')');
				lg.addColorStop(0.5, 'rgba(255,255,255,' + pulse.toFixed(3) + ')');
				lg.addColorStop(1, 'rgba(34,211,238,0)');
				ctx.strokeStyle = lg;
				ctx.lineWidth = k === 0 ? 1.8 : 1.2;
				ctx.beginPath(); ctx.moveTo(ex2, ey2); ctx.lineTo(ex, ey); ctx.stroke();
			}
		}

		// 四芒星爆闪（参考图二）：十字光芒 + 核心爆闪，用来给流星炸裂 / 亮星眨眼收尾
		function spawnFlare(x, y, scale) {
			flares.push({ x: x, y: y, life: 1, rot: Math.random() * Math.PI, s: scale || 1 });
		}

		function drawFlares() {
			for (var i = flares.length - 1; i >= 0; i--) {
				var f = flares[i];
				f.life -= 0.022;
				if (f.life <= 0) { flares.splice(i, 1); continue; }
				var k = 1 - f.life;                                   // 0 → 1 生长
				var ease = 1 - Math.pow(1 - k, 3);
				var len = (26 + 130 * f.s) * ease;
				var al = f.life * f.life;                             // 起得快、消得快
				for (var r = 0; r < 2; r++) {
					var aa = f.rot + r * Math.PI / 2;
					var L = r === 0 ? len : len * 0.58;
					var ex = f.x + Math.cos(aa) * L, ey = f.y + Math.sin(aa) * L;
					var ex2 = f.x - Math.cos(aa) * L * 0.62, ey2 = f.y - Math.sin(aa) * L * 0.62;
					var lg = ctx.createLinearGradient(f.x - L * 0.62, f.y - L * 0.62, ex, ey);
					lg.addColorStop(0, 'rgba(34,211,238,0)');
					lg.addColorStop(0.42, 'rgba(190,242,255,' + (0.45 * al).toFixed(3) + ')');
					lg.addColorStop(0.5, 'rgba(255,255,255,' + (0.95 * al).toFixed(3) + ')');
					lg.addColorStop(1, 'rgba(34,211,238,0)');
					ctx.strokeStyle = lg;
					ctx.lineWidth = (r === 0 ? 2.2 : 1.4) * (1.25 - k * 0.55);
					ctx.beginPath(); ctx.moveTo(ex2, ey2); ctx.lineTo(ex, ey); ctx.stroke();
				}
				var cr = 7 + 30 * ease * f.s;
				var cg = ctx.createRadialGradient(f.x, f.y, 0, f.x, f.y, cr);
				cg.addColorStop(0, 'rgba(255,255,255,' + (0.85 * al).toFixed(3) + ')');
				cg.addColorStop(0.4, 'rgba(190,242,255,' + (0.32 * al).toFixed(3) + ')');
				cg.addColorStop(1, 'rgba(34,211,238,0)');
				ctx.fillStyle = cg;
				ctx.beginPath(); ctx.arc(f.x, f.y, cr, 0, 6.2832); ctx.fill();
			}
		}

		function resize() {
			W = page.clientWidth || window.innerWidth;
			H = page.clientHeight || window.innerHeight;
			canvas.width = Math.round(W * dpr);
			canvas.height = Math.round(H * dpr);
			canvas.style.width = W + 'px';
			canvas.style.height = H + 'px';
			ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
			build();
			buildGrid();
			buildGalaxy();
			shots = [];
			bursts = [];
			flares = [];
			shotAcc = 0;
			if (reduce) draw(0, true);
		}

		// 流星"炸裂"：命中点炸出一圈火花 + 一圈冲击波环
		function spawnBurst(x, y) {
			if (!EFFECTS.meteorBurst) return;
			var n = 9 + (Math.random() * 8 | 0);
			for (var i = 0; i < n; i++) {
				var ang = Math.PI * 2 * (i / n) + rnd(-0.25, 0.25);
				var sp = rnd(0.5, 2.4);
				bursts.push({
					x: x, y: y,
					vx: Math.cos(ang) * sp, vy: Math.sin(ang) * sp,
					life: 1, r: rnd(0.5, 1.5),
					c: Math.random() < 0.7 ? CYAN : WHITE
				});
			}
			bursts.push({ ring: true, x: x, y: y, life: 1, rr: 1 });
			spawnFlare(x, y, 1);            // 炸裂的同时来一记四芒星爆闪
		}

		function drawBursts() {
			for (var i = bursts.length - 1; i >= 0; i--) {
				var b = bursts[i];
				if (b.ring) {
					b.rr += 3.2; b.life -= 0.035;
					if (b.life <= 0) { bursts.splice(i, 1); continue; }
					ctx.strokeStyle = 'rgba(34,211,238,' + (b.life * 0.55).toFixed(3) + ')';
					ctx.lineWidth = 1.4;
					ctx.beginPath();
					ctx.arc(b.x, b.y, b.rr, 0, 6.2832);
					ctx.stroke();
					continue;
				}
				b.x += b.vx; b.y += b.vy;
				b.vx *= 0.965; b.vy *= 0.965;          // 空气阻力：越飞越慢
				b.life -= 0.028;
				if (b.life <= 0) { bursts.splice(i, 1); continue; }
				var R = b.r * 6;
				var g = ctx.createRadialGradient(b.x, b.y, 0, b.x, b.y, R);
				g.addColorStop(0, 'rgba(' + b.c + ',' + (b.life * 0.5).toFixed(3) + ')');
				g.addColorStop(1, 'rgba(' + b.c + ',0)');
				ctx.fillStyle = g;
				ctx.beginPath(); ctx.arc(b.x, b.y, R, 0, 6.2832); ctx.fill();
				ctx.fillStyle = 'rgba(' + b.c + ',' + b.life.toFixed(3) + ')';
				ctx.beginPath(); ctx.arc(b.x, b.y, b.r, 0, 6.2832); ctx.fill();
			}
		}

		// 六边形网格（用户说的"6"那种效果）：静态纹理画到离屏画布，每帧只 blit，省 CPU；
		// 再叠几个会脉动的"数据节点"，让网格看起来是活的。
		function buildGrid() {
			grid = null;
			if (!EFFECTS.hexGrid || W === 0 || H === 0) return;
			var step = HEX.step, hh = step * Math.sqrt(3) / 2;
			var gw = Math.ceil(W / step) * step + step * 2;
			var gh = Math.ceil(H / hh) * hh + hh * 2;
			var off = document.createElement('canvas');
			off.width = Math.round(gw * dpr);
			off.height = Math.round(gh * dpr);
			var g2 = off.getContext('2d');
			if (!g2) return;
			g2.setTransform(dpr, 0, 0, dpr, 0, 0);
			g2.strokeStyle = 'rgba(34,211,238,' + HEX.alpha + ')';
			g2.lineWidth = 1;
			var R = step / Math.sqrt(3);
			for (var row = -1; row * hh < gh + hh; row++) {
				for (var col = -1; col * step * 1.5 < gw + step; col++) {
					var cx = col * step * 1.5;
					var cy = row * hh * 2 + (col % 2 ? hh : 0);
					g2.beginPath();
					for (var k = 0; k <= 6; k++) {
						var a = Math.PI / 180 * (60 * k - 30);
						var px = cx + R * Math.cos(a), py = cy + R * Math.sin(a);
						if (k === 0) g2.moveTo(px, py); else g2.lineTo(px, py);
					}
					g2.stroke();
				}
			}
			grid = off; gridW = gw; gridH = gh;

			hotNodes = [];
			for (var i = 0; i < HEX.hot; i++) {
				hotNodes.push({
					x: Math.random() * W, y: Math.random() * H,
					r: rnd(1.2, 2.4), ph: Math.random() * 6.2832, sp: rnd(0.5, 1.5)
				});
			}
		}

		function drawGrid(t, staticFrame, off) {
			if (!grid) return;
			var stepX = HEX.step * 1.5, stepY = HEX.step * Math.sqrt(3);
			var dx = -(off % stepX) - stepX;
			var dy = -(off % stepY) - stepY;
			ctx.drawImage(grid, dx, dy, gridW, gridH);

			for (var i = 0; i < hotNodes.length; i++) {
				var n = hotNodes[i];
				var a = staticFrame ? 0.5 : 0.30 + 0.50 * (0.5 + 0.5 * Math.sin(n.ph + t * n.sp));
				var R = n.r * 8;
				var g = ctx.createRadialGradient(n.x, n.y, 0, n.x, n.y, R);
				g.addColorStop(0, 'rgba(34,211,238,' + (a * 0.40).toFixed(3) + ')');
				g.addColorStop(1, 'rgba(34,211,238,0)');
				ctx.fillStyle = g;
				ctx.beginPath(); ctx.arc(n.x, n.y, R, 0, 6.2832); ctx.fill();
				ctx.fillStyle = 'rgba(190,242,255,' + a.toFixed(3) + ')';
				ctx.beginPath(); ctx.arc(n.x, n.y, n.r, 0, 6.2832); ctx.fill();
			}
		}

		function draw(t, staticFrame, off) {
			// ⚠️ 底色是**半透明**的（为了透出下面的 CSS 极光层），所以每帧必须先 clearRect：
			// 直接 fillRect 会把上一帧的内容一层层叠上来，几秒后整块变成死黑、星点全沉下去。
			ctx.clearRect(0, 0, W, H);
			ctx.fillStyle = BG;
			ctx.fillRect(0, 0, W, H);

			// —— 第一层：六边形网格（静态纹理 + 脉动节点）——
			if (EFFECTS.hexGrid) drawGrid(t, staticFrame, off || 0);

			// —— 第二层：螺旋星河（在星点下面，当"远景星空"）——
			if (EFFECTS.galaxy) drawGalaxy(t, staticFrame);

			// —— 第三层：星点（三层视差）——
			for (var i = 0; EFFECTS.stars && i < stars.length; i++) {
				var s = stars[i];
				// 呼吸：基础亮度 ±45%，用相位错开，整片不会一起闪
				var a = staticFrame ? s.a : s.a * (0.55 + 0.45 * Math.sin(s.ph + t * s.tw));
				if (a <= 0.02) continue;

				if (!staticFrame) {
					s.x += s.vx; s.y += s.vy;
					if (s.x < -6) s.x = W + 6; else if (s.x > W + 6) s.x = -6;
					if (s.y < -6) s.y = H + 6; else if (s.y > H + 6) s.y = -6;
				}

				if (s.glow) {
					var R = s.r * 5.5;
					var g = ctx.createRadialGradient(s.x, s.y, 0, s.x, s.y, R);
					g.addColorStop(0, 'rgba(' + s.c + ',' + (a * 0.32).toFixed(3) + ')');
					g.addColorStop(1, 'rgba(' + s.c + ',0)');
					ctx.fillStyle = g;
					ctx.beginPath();
					ctx.arc(s.x, s.y, R, 0, 6.2832);
					ctx.fill();
				}

				ctx.fillStyle = 'rgba(' + s.c + ',' + a.toFixed(3) + ')';
				ctx.beginPath();
				ctx.arc(s.x, s.y, s.r, 0, 6.2832);
				ctx.fill();
			}

			// —— 流星（静态帧不画，避免留下"卡住的一条线"）——
			if (staticFrame) return;
			for (var k = EFFECTS.meteors ? shots.length - 1 : -1; k >= 0; k--) {
				var m = shots[k];
				m.x += m.vx; m.y += m.vy; m.life -= 0.010;
				if (m.life <= 0 || m.x < -320 || m.x > W + 320 || m.y > H + 140) {
					// 寿命到了 = 飞到头了 ⇒ 原地炸裂（飞出画面的只剩消失，不用炸）
					if (m.life <= 0) spawnBurst(m.x, m.y);
					shots.splice(k, 1);
					continue;
				}
				var tail = m.len / Math.max(1, Math.abs(m.vx));
				var nx = m.x - m.vx * tail, ny = m.y - m.vy * tail;
				var lg = ctx.createLinearGradient(m.x, m.y, nx, ny);
				lg.addColorStop(0, 'rgba(190,242,255,' + (0.85 * m.life).toFixed(3) + ')');
				lg.addColorStop(0.35, 'rgba(34,211,238,' + (0.34 * m.life).toFixed(3) + ')');
				lg.addColorStop(1, 'rgba(34,211,238,0)');
				// —— 尾巴画成**锥形**：头部宽、往尾尖收成一点 ——
				// 等宽线看着像一根棍子（用户原话：尾巴是不是要宽、往下越细）
				var dl = Math.sqrt(m.vx * m.vx + m.vy * m.vy) || 1;
				var nx2 = -m.vy / dl, ny2 = m.vx / dl;       // 垂直方向
				var hw = m.w * (0.35 + 0.65 * m.life);       // 头部半宽（越到尾巴越细）
				ctx.fillStyle = lg;
				ctx.beginPath();
				ctx.moveTo(m.x + nx2 * hw, m.y + ny2 * hw);
				ctx.lineTo(m.x - nx2 * hw, m.y - ny2 * hw);
				ctx.lineTo(nx, ny);                           // 尾尖 = 一点
				ctx.closePath();
				ctx.fill();
				if (m.life > 0.25) {                           // 头部亮核（快消失时不画，避免"卡住一颗星"）
					var hg = ctx.createRadialGradient(m.x, m.y, 0, m.x, m.y, hw * 4.5);
					hg.addColorStop(0, 'rgba(255,255,255,' + (0.9 * m.life).toFixed(3) + ')');
					hg.addColorStop(0.35, 'rgba(190,242,255,' + (0.35 * m.life).toFixed(3) + ')');
					hg.addColorStop(1, 'rgba(34,211,238,0)');
					ctx.fillStyle = hg;
					ctx.beginPath(); ctx.arc(m.x, m.y, hw * 4.5, 0, 6.2832); ctx.fill();
				}
			}

			// —— 炸裂火花（画在流星之后，压在星点上）——
			drawBursts();
			drawFlares();
		}

		// —— 主循环：封顶 30fps + 不可见时暂停 ——
		var last = 0, raf = 0, running = false;
		function loop(ts) {
			if (!running) return;
			raf = requestAnimationFrame(loop);
			if (last === 0) last = ts;
			var dt = ts - last;
			if (dt < 33) return;                // 封顶 ~30fps
			last = ts;
			shotAcc += dt;
			if (shotAcc >= shotNext) { shotAcc = 0; shotNext = rnd(4000, 9000); spawnShot(); }
			// 亮星眨眼：随机挑一颗带柔光的星，来一记小爆闪（就是参考图二那种闪烁）
			if (EFFECTS.sparkle) {
				sparkleAcc += dt;
				if (sparkleAcc >= sparkleNext) {
					sparkleAcc = 0;
					sparkleNext = rnd(2200, 5200);
					for (var z = 0; z < 24; z++) {
						var s2 = stars[(Math.random() * stars.length) | 0];
						if (s2 && s2.glow) { spawnFlare(s2.x, s2.y, 0.42); break; }
					}
				}
			}
			gridOff += HEX.drift * (dt / 33);   // 网格缓慢漂移（负数方向也看不出接头）
			draw(ts / 1000, false, gridOff);
		}
		function start() { if (!running && !reduce) { running = true; last = 0; raf = requestAnimationFrame(loop); } }
		function stop() { running = false; if (raf) cancelAnimationFrame(raf); raf = 0; }

		var rt = 0;
		window.addEventListener('resize', function () {
			clearTimeout(rt);
			rt = setTimeout(function () { resize(); if (reduce) draw(0, true); }, 150);
		});
		document.addEventListener('visibilitychange', function () {
			if (document.hidden) stop(); else start();
		});

		resize();
		if (reduce) draw(0, true); else start();
	}

	if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot);
	else boot();
})();
