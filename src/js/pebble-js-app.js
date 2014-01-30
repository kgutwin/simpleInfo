var iconMap = {
	'UNKNOWN': 0,
	'clear-day': 1, 
	'clear-night': 2, 
	'rain': 3, 
	'snow': 4, 
	'sleet': 5, 
	'wind': 6, 
	'fog': 7, 
	'cloudy': 8, 
	'partly-cloudy-day': 9, 
	'partly-cloudy-night': 10
};

var lastWeather = {
	'icon': 0,
	'temp': "",
	'alerts': 0
};

function sendWeather(icon, temp, alerts) {
	if (icon != lastWeather.icon || temp != lastWeather.temp || alerts != lastWeather.alerts) {
		console.log("sendWeather chg " + icon + " " + temp + " " + alerts);
		Pebble.sendAppMessage({
			"wxCurIcon":icon,
			"wxCurTemp":temp + "\u00B0",
			"wxCurAlerts":alerts
		});
		lastWeather.icon = icon;
		lastWeather.temp = temp;
		lastWeather.alerts = alerts;
	}
}

function fetchWeather(latitude, longitude) {
	var response;
	var req = new XMLHttpRequest();
	req.open('GET', "http://api.forecast.io/forecast/1512d187338a135bfc8a2b5765c78e46/" +
		latitude + "," + longitude + "?exclude=minutely,hourly,daily,flags", true);
	req.onload = function(e) {
		if (req.readyState == 4) {
			if(req.status == 200) {
				console.log(req.responseText);
				response = JSON.parse(req.responseText);
				var temperature, iconInt, alerts;
				if (response && response.currently) {
					var weatherResult = response.currently;
					temperature = Math.round(weatherResult.temperature);
					if (weatherResult.icon in iconMap) {
						iconInt = iconMap[weatherResult.icon];
					} else {
						iconInt = 0;
					}
					console.log(temperature);
					console.log(iconInt);
					if (response.alerts) {
						alerts = response.alerts.length;
					} else {
						alerts = 0;
					}
					sendWeather(iconInt, temperature, alerts);
				}
			} else {
				console.log("Error " + req.status);
				sendWeather(0, "N/R", 0);
			}
		}
	};
	req.send(null);
}

function locationSuccess(pos) {
	var coordinates = pos.coords;
	console.log("locationSuccess " + coordinates.latitude + "," + coordinates.longitude);
	fetchWeather(coordinates.latitude, coordinates.longitude);
}

function locationError(err) {
	console.warn('location error (' + err.code + '): ' + err.message);
	sendWeather(0, "N/L", 0);
}

var locationOptions = { "timeout": 15000, "maximumAge": 60000 }; 

function tryWeather() {
	console.log("tryWeather");
	navigator.geolocation.getCurrentPosition(locationSuccess, locationError, locationOptions);
}

var lastCalendar = {
	"calCurText": "",
	"calCurIcon": -1,
	"calCurStart": 0
};

function sendCalendar(icon, text, start) {
	if (icon != lastCalendar.calCurIcon || text != lastCalendar.calCurText || start != lastCalendar.calCurStart) {
		lastCalendar.calCurText = text;
		lastCalendar.calCurIcon = icon;
		lastCalendar.calCurStart = start;
		console.log("sendCalendar " + JSON.stringify(lastCalendar));
		Pebble.sendAppMessage(lastCalendar);
	}
}

function renderTime(s) {
	var date = new Date(s * 1000);
	var hh = date.getHours();
	var mm = date.getMinutes();
	var ampm = "a";
	if (hh > 12) {
		ampm = "p";
		hh -= 12;
	}
	if (mm < 10) { mm = "0" + mm; }
	return hh + ":" + mm + ampm;
}

function tryCalendar() {
	var response;
	var req = new XMLHttpRequest();
	req.open('GET', "http://www.gutwin.org/ebw/biib.json?cache=" + (Math.random() * 100000), true);
	req.onload = function(e) {
		if (req.readyState == 4) {
			if(req.status == 200) {
				console.log(req.responseText);
				response = JSON.parse(req.responseText);
				var best = { "icon":-1, "text": "", "start": 2147483647 };
				for (var i = 0; i < response.mtgs.length; i++) {
					var o = response.mtgs[i];
					var now = new Date().getTime() / 1000;
					if ((o.start + (13 * 60)) >= (now) && 
						(o.start - (125 * 60)) <= (now) &&
						(o.start < best.start)) {
						best.start = o.start;
						best.text = o.subject + "\n" + o.location + "\n" + renderTime(o.start);
						if (o.icon == "lync") {
							best.icon = 1;
						} else {
							best.icon = 0;
						}
					}
				}
				sendCalendar(best.icon, best.text, best.start);
			} else {
				console.log("Error " + req.status);
				sendCalendar(-1, "", 0);
			}
		} else {
			console.log("readyState " + req.readyState);
		}
	};
	req.send(null);
}

Pebble.addEventListener(
	"ready",
	function(e) {
		console.log("CONNECTION ESTABLISHED " + e.ready);
	});

Pebble.addEventListener(
	"appmessage",
	function(e) {
		console.log("RECEIVED MESSAGE:");
		console.log(JSON.stringify(e));
		if (e.payload.wxGet) {
			tryWeather();
		}
		if (e.payload.calGet) {
			tryCalendar();
		}
	});

