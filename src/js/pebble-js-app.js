function iconFromWeatherId(weatherId) {
  if (weatherId < 600) {
    return 2;
  } else if (weatherId < 700) {
    return 3;
  } else if (weatherId > 800) {
    return 1;
  } else {
    return 0;
  }
}

function fetchWeather(latitude, longitude) {
  var response;
  var req = new XMLHttpRequest();
  req.open('GET', "http://api.openweathermap.org/data/2.1/find/city?" +
    "lat=" + latitude + "&lon=" + longitude + "&cnt=1", true);
  req.onload = function(e) {
    if (req.readyState == 4) {
      if(req.status == 200) {
        console.log(req.responseText);
        response = JSON.parse(req.responseText);
        var temperature, icon;
        if (response && response.list && response.list.length > 0) {
          var weatherResult = response.list[0];
          temperature = Math.round(weatherResult.main.temp - 273.15);
          icon = iconFromWeatherId(weatherResult.weather[0].id);
          console.log(temperature);
          console.log(icon);
          Pebble.sendAppMessage({
            "WX_CUR_ICON":icon,
            "WX_CUR_TEMP":temperature + "\u00B0C"});
        }

      } else {
        console.log("Error");
      }
    }
  };
  req.send(null);
}

function locationSuccess(pos) {
  var coordinates = pos.coords;
  fetchWeather(coordinates.latitude, coordinates.longitude);
}

function locationError(err) {
  console.warn('location error (' + err.code + '): ' + err.message);
  Pebble.sendAppMessage({
    "WX_CUR_TEMP":"N/L"
  });
}

var locationOptions = { "timeout": 15000, "maximumAge": 60000 }; 


Pebble.addEventListener(
	"ready",
	function(e) {
		console.log("connect!" + e.ready);
		console.log(e.type);
	});

Pebble.addEventListener(
	"appmessage",
	function(e) {
		window.navigator.geolocation.getCurrentPosition(locationSuccess, locationError, locationOptions);
		console.log(e.type);
		console.log(e.payload.temperature);
		console.log("message!");
	});

