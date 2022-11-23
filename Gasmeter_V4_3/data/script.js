// Complete project details: https://randomnerdtutorials.com/esp32-plot-readings-charts-multiple/

// Get current sensor readings when the page loads
window.addEventListener('load', getReadings);

//Plot gasforbrug in chart
function plotGasforbrug(objData) {
// https://www.highcharts.com/docs/index
// http://www.java2s.com/example/javascript/highcharts/column-chart-index.html
// http://www.java2s.com/example/javascript/highcharts/creating-series-for-column-chart.html

  var series = [];
  categories = [];
  for (var key in objData) {
    var obj = objData[key];
    categories.push(key);
    for (var item in obj) {
      var targetSeries ;
      var bFound = false;
      for(var i=0; i< series.length; i++){
        if(item == series[i].name){
          bFound = true;
          targetSeries = series[i];
        }
      }
      if (!bFound) {
          targetSeries = {'name': item, 'data':[]};
          series.push(targetSeries);
      }
      var val = obj[item]
      targetSeries.data.push(val);
    }
  }
  console.log(series);
  console.log(categories);
  // Create gasForbrug Chart
  var chartT = new Highcharts.Chart({
    chart:{
      renderTo:'chart-gasForbrug',
      type: 'column'
    },
    tooltip: {
      backgroundColor: '#FCFFC5',
      borderColor: 'black',
      borderRadius: 10,
      borderWidth: 3
    },
    title: {
      // ÆØÅ - Se: https://www.nemprogrammering.dk/wp/ae-o-og-a-pa-hjemmesider/
//      text: 'Sidste m&aring;neds afl&aelig;sninger'
	text: 'Last 30 days'
    },
    xAxis: {
      categories: categories
    },
    yAxis: {
      min: 0,
      title: {
        text: 'Gas m3'
      }
    },
    tooltip: {
        headerFormat: '<span style="font-size:15px">{point.key}</span><table>',
        pointFormat:  '<tr>' +
                        '<td><b>{point.y:.3f} m3</b></td>' +
                      '</tr>',
        footerFormat: '</table>',
        shared: true,
        useHTML: true
    },
    plotOptions: {
	series: {
	    color: '#000fff'
	},
        column: {
            pointPadding: 0.2,
            borderWidth: 0
        }
    },
    series: series,
    credits: {
      enabled: false
    }
  });
}

// Function to get current readings on the webpage when it loads for the first time
function getReadings(){
  var xhr = new XMLHttpRequest();
  xhr.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      var myObj = JSON.parse(this.responseText);
      console.log(myObj);
      plotGasforbrug(myObj);
    }
  };
  xhr.open("GET", "/readings", true);
  xhr.send();
}

if (!!window.EventSource) {
  var source = new EventSource('/events');

  source.addEventListener('open', function(e) {
    console.log("Events Connected");
  }, false);

  source.addEventListener('error', function(e) {
    if (e.target.readyState != EventSource.OPEN) {
      console.log("Events Disconnected");
    }
  }, false);

  source.addEventListener('message', function(e) {
    console.log("message", e.data);
  }, false);

  source.addEventListener('new_readings', function(e) {
    console.log("new_readings", e.data);
    var myObj = JSON.parse(e.data);
    console.log(myObj);
    plotGasforbrug(myObj);
  }, false);
}


// =========================== WebSocket ============================
var gateway = `ws://${window.location.hostname}/ws`;
var websocket;

window.addEventListener('load', onLoad);

function initWebSocket() {
  console.log('Trying to open a WebSocket connection...');
  websocket           = new WebSocket(gateway);
  websocket.onopen    = onOpen;
  websocket.onclose   = onClose;
  websocket.onmessage = onMessage; // <-- add this line
}
function onOpen(event) {
  console.log('Connection opened');
  notifyClients();
}
function onClose(event) {
  console.log('Connection closed');
  setTimeout(initWebSocket, 2000);
}
function onMessage(event) {
  var state;
  state = event.data;
  document.getElementById('state').innerHTML = state;
}
function onLoad(event) {
  initWebSocket();
}
