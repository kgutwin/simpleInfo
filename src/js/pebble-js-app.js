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

var weatherIter = 0;
function tryWeather(iter) {
	console.log("tryWeather iter " + iter);
	if (iter === 0) iter = weatherIter;
	if (iter == weatherIter) {
		navigator.geolocation.getCurrentPosition(locationSuccess, locationError, locationOptions);
		setTimeout(function() { tryWeather( iter+1 ); }, 120000);
		weatherIter = iter + 1;
	}
}

Pebble.addEventListener(
	"ready",
	function(e) {
		console.log("connect!" + e.ready);
	});

Pebble.addEventListener(
	"appmessage",
	function(e) {
		console.log("message!");
		console.log(JSON.stringify(e));
		if (e.payload.wxGet) {
			tryWeather(0);
		}
	});

