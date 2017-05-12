function (event) {

	/* constants */

	var log1k = Math.log (1000.0);
	var rate  = 48000;

	/* some helper functions */

	function freq_at_x (x, width) {
		/* log-scale 20..20K */
		return 20 * Math.pow (1000, x / width);
	}

	function x_at_freq (f, width) {
		return width * Math.log (f / 20.0) / log1k;
	}

	function y_pos (p) {
		return 175 - 175 * p;
	}

	function y_at_db (db) {
		return y_pos(1 + db / 96);
	}


	/* the actual SVG drawing function */
	function x42_draw_spectrum (sd) {
		var ds = sd.data ('xModPorts');
		var svg = sd.svg ('get');
		if (!svg) { return; }

		var width = 256;
		var height = 175;
		svg.clear ();

		/* grid */
		var yg;
		var g = svg.group ({stroke: 'gray', strokeWidth: 0.25, fill: 'none'});
		yg = .5 + Math.round (y_at_db (  0)); svg.line (g, 0, yg, width, yg);
		yg = .5 + Math.round (y_at_db ( -6)); svg.line (g, 0, yg, width, yg);
		yg = .5 + Math.round (y_at_db (-12)); svg.line (g, 0, yg, width, yg);
		yg = .5 + Math.round (y_at_db (-18)); svg.line (g, 0, yg, width, yg);
		yg = .5 + Math.round (y_at_db (-24)); svg.line (g, 0, yg, width, yg);
		yg = .5 + Math.round (y_at_db (-48)); svg.line (g, 0, yg, width, yg);
		yg = .5 + Math.round (y_at_db (-72)); svg.line (g, 0, yg, width, yg);

		var xg;
		g = svg.group ({stroke: 'darkgray', strokeWidth: 0.25, strokeDashArray: '1, 3', fill: 'none'});
		xg = Math.round (x_at_freq (   50, width)); svg.line (g, xg, 0, xg, height);
		xg = Math.round (x_at_freq (  200, width)); svg.line (g, xg, 0, xg, height);
		xg = Math.round (x_at_freq (  500, width)); svg.line (g, xg, 0, xg, height);
		xg = Math.round (x_at_freq ( 2000, width)); svg.line (g, xg, 0, xg, height);
		xg = Math.round (x_at_freq ( 5000, width)); svg.line (g, xg, 0, xg, height);
		xg = Math.round (x_at_freq (15000, width)); svg.line (g, xg, 0, xg, height);

		var tg = svg.group ({stroke: 'gray', fontSize: '8px', textAnchor: 'end', fontFamily: 'Monospace', strokeWidth: 0.5});
		var to; var tr;
		g = svg.group ({stroke: 'gray', strokeWidth: 0.25, strokeDashArray: '3, 2'});
		xg = Math.round (x_at_freq (  100, width)); svg.line (g, xg, 0, xg, height + 5);
		to = svg.group (tg, {transform: 'translate ('+xg+', '+(height + 3)+')'});
		tr = svg.group (to, {transform: 'rotate (-90, 3, 0)'});
		svg.text (tr, 0, 0, "100");

		xg = Math.round (x_at_freq ( 1000, width)); svg.line (g, xg, 0, xg, height + 5);
		to = svg.group (tg, {transform: 'translate ('+xg+', '+(height + 3)+')'});
		tr = svg.group (to, {transform: 'rotate (-90, 3, 0)'});
		svg.text (tr, 0, 0, "1K");

		xg = Math.round (x_at_freq (10000, width)); svg.line (g, xg, 0, xg, height + 5);
		to = svg.group (tg, {transform: 'translate ('+xg+', '+(height + 3)+')'});
		tr = svg.group (to, {transform: 'rotate (-90, 3, 0)'});
		svg.text (tr, 0, 0, "10K");

		yg = 3 + Math.round (y_at_db (-6)); svg.text (tg, width - 5, yg, "-6dBFS");
		yg = 3 + Math.round (y_at_db (-18)); svg.text (tg, width - 5, yg, "-18dBFS");
		yg = 3 + Math.round (y_at_db (-48)); svg.text (tg, width - 5, yg, "-48dBFS");
		yg = 3 + Math.round (y_at_db (-72)); svg.text (tg, width - 5, yg, "-72dBFS");

		/* transfer function */
		var clp = svg.clipPath (null, 'tfClip');
		svg.rect (clp, -1, 0, width + 3, height);

		var color = 'white';
		if (1 == ds[':bypass']) {
			color = '#444444';
		}

		var path = [];
		g = svg.group ({stroke: color, strokeWidth: 1.0, fill: 'none'});
		for (var x = 0; x < width; x++) {
			path.push ([x, y_pos (ds['bin' + (x + 1)])]);
		}
		svg.polyline (g, path, {clipPath: 'url(#tfClip)'});
		path.push ([width + 1, height]);
		path.push ([0, height]);
		g = svg.group ({stroke: 'none', fill: color, fillOpacity: '0.35'});
		svg.polyline (g, path, {clipPath: 'url(#tfClip)'});
	}

	function handle_event (symbol, value) {
		;
	}

	if (event.type == 'start') {
		var sd = event.icon.find ('[mod-role=spectrum-display]');
		sd.svg ();

		var svg = sd.svg ('get');
		svg.configure ({width: '256px'}, false);
		svg.configure ({height: '205px'}, false);
		svg.clear ();
		var tg = svg.group ({stroke: '#cccccc', fontSize: '11px', textAnchor: 'middle', strokeWidth: 0.5});
		svg.text (tg, 59, 65, "Spectrum", {dy: '-1.5em'});
		svg.text (tg, 59, 65, "Display");
		svg.text (tg, 59, 65, "MOD v0.15.0 or later.", {dy: '1.5em'});


		var ds = {};
		var ports = event.ports;
		for (var p in ports) {
			ds[ports[p].symbol] = ports[p].value;
		}

		sd.data ('xModPorts', ds);
		x42_draw_spectrum (sd);

	} else if (event.type == 'change') {
		var sd = event.icon.find ('[mod-role=spectrum-display]');
		var ds = sd.data ('xModPorts');
		ds[event.symbol] = event.value;
		sd.data ('xModPorts', ds);
		x42_draw_spectrum (sd);
	}
}
