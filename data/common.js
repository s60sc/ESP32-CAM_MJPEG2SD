
        // s60sc 2023

        /*********** initialisation  ***********/

        'use strict'
             
        const isSecure = window.location.protocol == 'https:' ? true : false;
        const defaultPort = window.location.protocol == 'https:' ? 443 : 80;
        const webPort = !window.location.port ? defaultPort : window.location.port;
        const baseHost = document.location.hostname;
        const webHost = window.location.protocol + "//" + baseHost;
        const webServer = webHost + ":" + webPort;
        const wsServer = (isSecure ? "wss" : "ws") + "://" + baseHost + ":" + webPort + "/ws";

        let ws = null;
        let hbTimer = null;
        let updateData = {}; // receives json for status data as key val pairs
        let statusData = {}; // stores all status data as key val pairs
        let cfgGroupNow = -1;
        let loggingOn = false;
        const CLASS = 0;
        const ID = 1;
        const $ = document.querySelector.bind(document);
        const $$ = document.querySelectorAll.bind(document);
        const baseFontSize = parseInt(getComputedStyle(document.documentElement).fontSize);
        const root = getComputedStyle($(':root'));
        const bigThumbSize = parseFloat(root.getPropertyValue('--bigThumbSize')) * baseFontSize;
        const smallThumbSize = parseFloat(root.getPropertyValue('--smallThumbSize')) * baseFontSize;
        let isImmed = false;
        let logType = appLogInit;
        
        async function initialise() {
          try {
            await sleep(500);
            addButtons();
            addRangeData();
            if (doCustomInit) customInit();
            setListeners();
            doLoadStatus ? loadStatus("") : configStatus(false); 
            if (doRefreshTimer && hbTimer == null) setTimeout(refreshStatus, refreshInterval);
            if (doInitWebSocket) initWebSocket();
          } catch (error) {
            showLog("Error: " + error.message);
            alert("Error: " + error.message);
          } 
        }
        
        /*********** websocket functions ***********/
              
        // define websocket handling
        function initWebSocket() {
          if (ws == null) {
            ws = new WebSocket(wsServer);
            ws.onopen = onOpen;
            ws.onclose = onClose;
            ws.onmessage = onMessage; 
            ws.onerror = onError;
          }
          showLog("Connect to: " + wsServer);
        }
          
        // periodically check that connection is still up and get status
        function heartbeat() {
          if (!ws) return;
          if (ws.readyState !== 1) return;
          sendCmd("H");
          clearTimeout(hbTimer);
          hbTimer = setTimeout(heartbeat, refreshInterval);  
        }
        
        // connect to websocket server
        function onOpen(event) {
          showLog("Connected");
          if (doCustomSync) customSync();
          if (doHeartbeat) heartbeat();
        }
        
        // process received WS message
        function onMessage(messageEvent) {
          if (messageEvent.data.startsWith("{")) {
            // json data
            updateData = JSON.parse(messageEvent.data);
            let filter = updateData.cfgGroup;
            delete updateData.cfgGroup;
            if (filter == "-1") updateStatus(); // status update
            else buildTable(updateData, filter); // format received config json into html table
          } else showLog(messageEvent.data, false);
        }
        
        function onError(event) {
          showLog("WS Error: " + event.code);
        }
        
        function onClose(event) {
          showLog("Disconnected: " + event.code + ' - ' + event.reason);
          loggingOn = false;
          ws = null;
          // event.codes:
          //   1006 if server not available, or another web page is already open
          //   1005 if closed from app
          if (event.code == 1006) {}
          else if (event.code != 1005) initWebSocket(); // retry if any other reason
        }
        
        async function closeWS() {
          ws.send('K');
          await sleep(500);
          if (ws != null) ws.close();
        }
        
        /*********** page layout functions ***********/
      
        function openTab(e) {
          // control tab viewing
          $$('.tabcontent').forEach(el => { el.style.display = "none"; });
          $('#' + e.name).style.display = "inherit";  
          $$('.tablinks').forEach(el => {el.classList.remove("active");});
          e.classList.add("active");
          try {
            if (e.name == 'mainPage') show($('#main'));
            else hide($('#main'));
          } catch {}
        }

        function accordian(accId) {
          // accordian buttons to show / hide elements
          let panel = $('#' + accId);
          if (panel.style.display === "inherit") panel.style.display = "none";
          else panel.style.display = "inherit";
        }
       
         function rangeSlider(el, isPos = true, statusVal = null) {
          // update range slider marker position and value 
          const rangeVal = el.parentElement.children.rangeVal;
          if (statusVal != null) rangeVal.innerHTML = statusVal;
          const currVal = isPos ? parseFloat(el.value) : parseFloat(rangeVal.innerHTML);
          const minval = parseFloat(el.min);
          const maxval = parseFloat(el.max);
          const decPlaces = (el.step > 0 && el.step < 1) || el.step == 'any' ? 1 : 0;
          if (el.classList.contains('logslider')) {
            // range value is logarithmic
            const minlog = Math.log(minval);
            const maxlog = Math.log(maxval) ;
            const scale = (maxlog - minlog) / (maxval - minval);
            // if isPos then get value from slider positional change by user, else set slider position from initial value.
            if (isPos) rangeVal.innerHTML = Math.exp((currVal - minval) * scale + minlog).toFixed(decPlaces);
            else el.value = minval + ((currVal == 0 ? 0 : Math.log(currVal)) - minlog) / scale; 
          } else {
            el.value = parseFloat(currVal).toFixed(decPlaces);
            rangeVal.innerHTML = el.value;
          }
          el.setAttribute('value', rangeVal.innerHTML);

          // position of range marker relative to slider thumb
          const rangeThumbSize = el.classList.contains('bigThumb') ? bigThumbSize : smallThumbSize;
          let markerRange = el.offsetWidth - rangeThumbSize;
          let position = markerRange * (el.value - minval) / (maxval - minval); 

          // calculate absolute marker position for orientation of slider
          if (el.classList.contains('vertical')) {
            rangeVal.style.top = el.offsetTop + markerRange/2 - position + 'px';
            rangeVal.style.left = el.offsetLeft + markerRange/2 - (rangeVal.offsetWidth - rangeThumbSize)/2 + 'px'; // 
          } else if (el.classList.contains('vertInv')) {
            rangeVal.style.top = el.offsetTop + position - markerRange/2 + 'px';
            rangeVal.style.left = el.offsetLeft + markerRange/2 - (rangeVal.offsetWidth - rangeThumbSize)/2 + 'px';
          } else rangeVal.style.left = el.offsetLeft + position - (rangeVal.offsetWidth - rangeThumbSize)/2 + 'px'; // default horizontal

        }
        
        let rangeObserver = new IntersectionObserver ( function(entries) {
          // recalc each range slider that becomes visible
            entries.forEach(el => { if (el.isIntersecting === true) rangeSlider(el['target']); });
          }, { threshold: [0] }
        );
        $$('input[type=range]').forEach(el => { rangeObserver.observe(el); });
        
        let logObserver = new IntersectionObserver (entries => {
          // refresh log when becomes visible
          entries.forEach(entry => { if (entry.isIntersecting === true) getLog(); });
        }); 
        logObserver.observe($('#appLog'));
        
        function addButtons() {
          // add commmon buttons to relevant sections
          $$('.addButtons').forEach(el => {
            el.innerHTML = '<section id="buttons">'
              +'<button id="save" style="float:right;" value="1">Save Settings</button>'
              +'<button id="reset" style="float:right;" value="1">Reboot ESP</button>'
            +'</section><br>'
          });
        }
        
         function addRangeData() {
          // add labelling for rangle sliders
          $$('input[type="range"]').forEach(el => {
            if (el.classList.contains('vertical')) el.style.transform = 'rotate(270deg)'; 
            else if (el.classList.contains('vertInv')) el.style.transform = 'rotate(90deg)';
            if (!el.classList.contains('ignore')) {
              if (!isDefined(el.parentElement.children.rangeMin)) el.insertAdjacentHTML("beforebegin", '<div name="rangeMin"/>'+el.min+'</div>'); 
              el.insertAdjacentHTML("afterend", '<div name="rangeVal">'+el.value+'</div>');
              if (!isDefined(el.parentElement.children.rangeMax)) el.insertAdjacentHTML("afterend", '<div name="rangeMax"/>'+el.max+'</div>');
            }
            rangeSlider(el, false);
          });
        } 

        /*********** data processing functions ***********/
        
        async function loadStatus(specifier) {
          // request and load current status from app
          const response = await fetch(webServer+'/status'+specifier);
          if (response.ok) {
            updateData = await response.json();
            updateStatus();
            await sleep(1000);
          } else alert(response.status + ": " + response.statusText);  
        }
        
        function refreshStatus() {
          // refresh status at required interval
          clearTimeout(hbTimer);
          doLoadStatus ? loadStatus("?q") : configStatus(true); 
          hbTimer = setTimeout(refreshStatus, refreshInterval);
        }
        
        function updateStatus() {
          // replace each existing value with new received value, using key name to match html tag id
          Object.entries(updateData).forEach(([key, value]) => {
            let elt = $('text#'+key); // svg button
            let eld = $('div#'+key); // display text
            let eli = $('#'+key); // input field
            if (elt) elt.textContent = value; 
            else if (eld) {if (eld.classList.contains('displayonly')) eld.innerHTML = value;} // display text 
            else if (eli != null) { // input fields
              if (eli.type === 'checkbox') eli.checked = !!Number(value);
              else if (eli.type === 'range') eli.setAttribute('value', value);
              else if (eli.type === 'option') eli.selected = true;
              else eli.value = value; 
            }
            let elth = $('td#'+key); 
            if (elth != null) elth.innerHTML = value; // table data
            $$('input[name="' + key + '"]').forEach(el => {if (el.value == value) el.checked = true;}); // radio button group
            statusData[key] = value;
            processStatus(ID, key, value, false);
          });
          $$('input[type=range]').forEach(el => {rangeSlider(el, false, el.getAttribute('value'));});  // initialise range sliders
        }
        
        async function sendUpdates(doAction) {    
          // send bulk updates to app as json 
          statusData['action'] = doAction;
          const response = await fetch(webServer + '/update', {
            method: 'POST', 
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify(statusData),
          });
          if (!response.ok) alert(response.status + ": " + response.statusText); 
        } 
        
        /*********** utility functions ***********/
        
        function debounce(func, timeout = 500){
          // debounce rapid clicks to prevent unnecessary fetches
          let timer;
          return (...args) => {
            clearTimeout(timer);
            timer = setTimeout(() => { func.apply(this, args); }, timeout);
          };
        }
        
        const debounceSendControl = debounce((key, value) => sendControl(key, value));
        
        function sleep(ms) {
          return new Promise(resolve => setTimeout(resolve, ms));
        }
        
        async function fetchRetry(url, options, interval, timeout) {
          let response;
          let retries = Math.ceil(timeout / interval);
          while (retries--) {
            try {
              response = await fetch(url, options);
              if (response.ok) {
                sleep(interval);
                return response;
              }
            } catch {}
            await new Promise((resolve) => setTimeout(resolve, interval));
          }
          return response;
        }
              
        function hide(el) {
          el.classList.add('hidden')
          el.style.display = "none";
        }
        
        function show(el) {
          el.classList.remove('hidden')
          el.style.display = "";
        }

        function disable(el) {
          el.classList.add('disabled');
          el.disabled = true;
        }

        function enable(el) {
          el.classList.remove('disabled');
          el.disabled = false;
        }
        
        function isActive(el) {
          return el.classList.contains('active') ? true : false;
        }
        
        function isHidden(el) {
           return el.classList.contains("hidden") ? true : false;
        }
        
        function isDefined(variable) {
          return (typeof variable === 'undefined' || variable === null ) ? false : true;
        }
        
        async function showAlert(value) {
          $('#alertText').innerHTML = value;
          await sleep(5000);
          $('#alertText').innerHTML = "";
        }
        
        async function saveChanges() {
          // save change and reboot
          await sleep(100);
          sendControl('save', 1);
          await sleep(1000);
          sendControl('reset', 1);
        }
        
        function dbg(msg) {
          console.log('***** '+msg);
        }
        
        function dbgTrail(msg) {
          // get trail of function calls
          dbg(msg);
          const stackTrace = new Error().stack;
          console.log("Function call trail: " + stackTrace);
        }

        function clearLog() {
          if (window.confirm('This will delete all log entries. Are you sure ?')) { 
            $('#appLog').innerHTML = "";
            if (logType != 1) sendControl("resetLog", "1");
          }
        }
        
        async function getLog() {
          // request display of stored log file
          const log = $('#appLog');
          log.innerHTML = "";
          loggingOn = (logType == 1) ? true : false; 
          if (logType != 1) {
            const requestURL = logType == 0 ? '/control?displayLog=1' : '/web?log.txt';
            const response = await fetch(encodeURI(requestURL));
            if (response.ok) {
              const logData = await response.text();
              let start = 0;
              const loadNextLine = () => {
                const index = logData.indexOf("\n", start);
                if (index !== -1) {
                  log.innerHTML += colorise(logData.substring(start, index)) + '<br>';
                  start = index + 1;
                  // auto scroll log as loaded
                  const bottom = 2 * baseFontSize;// 2 lines
                  const pos = Math.abs(log.scrollHeight - log.clientHeight - log.scrollTop);
                  if (pos < bottom) log.scrollTop = log.scrollHeight;
                  // stop browser hanging while log is loaded
                  setTimeout(loadNextLine, 1); 
                } 
              };
              loadNextLine(); 
            } else showAlert(response.status + ": " + response.statusText); 
          }
        }
        
        function checkTime(value) {
          // sync browser time with app
          let now = new Date();
          let nowUTC = Math.floor(now.getTime() / 1000);
          let timeDiff = Math.abs(nowUTC - value);  
          if (timeDiff > 5) sendControl("clockUTC", nowUTC); // 5 secs 
        }
        
        function setTz(value) {
          $('#timezone').value = value;
          sendControl('timezone', value);
          return false;
        }
        
        /*********** command processing ***********/
        
        function setListeners() {
        
          // click events
         document.addEventListener("click", function (event) {
            const e = event.target;
            // svg rect elements, use id of its following text node
            if (e.nodeName == 'rect') processStatus(ID, e.nextElementSibling.id, 1);
            // tab buttons, use name as target id 
            else if (e.classList.contains('tablinks')) openTab(e);
            // other buttons
            else if (e.tagName == 'BUTTON') processStatus(ID, e.id, e.value);
            // navigation and presentation icons
            else if (e.tagName == 'NAV' || e.tagName == 'DIV') processStatus(CLASS, e.classList.value, e.id);
            else if (e.nodeName == 'INPUT') {
              if (e.type === 'button') processStatus(ID, e.id, e.value);
              else {/*ignore*/}
            }
            else if (e.nodeName == 'SELECT') {/*ignore*/}
          });
          
          // change events
          document.addEventListener("change", function (event) {
            const e = event.target;
            const value = e.value.trim();
            const et = event.target.type;
            // input fields of given class 
            if (e.nodeName == 'INPUT') {
              if (e.type === 'checkbox') processStatus(ID, e.id, e.checked ? 1 : 0);
              else if (et === 'button' || et === 'file') processStatus(ID, e.id, 1);
              else if (et === 'radio') { if (e.checked) processStatus(ID, e.name, value); } 
              else if (et === 'range') processStatus(ID, e.id, e.parentElement.children.rangeVal.innerHTML); 
              else if (e.hasAttribute('id')) processStatus(ID, e.id, value);
            }
            else if (e.tagName == 'SELECT') processStatus(ID, e.id, value);
          });
          
          // input events
          document.addEventListener("input", function (event) {
            const e = event.target;
            if (e.type === 'range') {
              rangeSlider(e);
              // for element with class='immed' send data for processing immediately
              if (e.classList.contains('immed')) {
                isImmed = true;
                processStatus(ID, e.id, e.parentElement.children.rangeVal.innerHTML);
              }
            }
          });
          
          // user command entered on Log tab
          document.addEventListener("keydown", function (event) {
            if (event.target.id == 'txtCmd') {
              let keyPress = event.keyCode || event.which;
              if (keyPress == 13) sendWsCmd();
            }
          });
          
          // move away from browser tab
          document.addEventListener('visibilitychange', () => {
            if (document.hidden) closedTab(false); // app specific
          });
          
          // recalc range marker positions 
          window.addEventListener('resize', function (event) {
            $$('input[type=range]').forEach(el => { rangeSlider(el); });
          });
          
          // close web socket on closing browser tab
          window.addEventListener('beforeunload', function (event) {
            if (ws) closeWS();
            closedTab(true); // app specific 
          });   
          
        }
        
        function sendWsCmd() {
          // send user command to websocket server
          let txt = $('#txtCmd');
          let line = txt.value;
          if (line != "" && ws !== undefined) {
            sendCmd(line);
            txt.value = "";
            txt.focus();
          } else showLog("No command or no connection");
        }
        
        function sendCmd(reqStr) {
          ws.send(reqStr);
          showLog("Cmd: " + reqStr);
        }
        
        function showLog(reqStr, fromUser = true) {
          if (loggingOn) {
            let date = new Date();
            // add timestamp to received text if generated by browser
            let logText = fromUser ? "[" + date.toLocaleTimeString() + " Web] " : "";
            logText += reqStr;
            // append to log display 
            let log = $('#appLog');
            log.innerHTML += colorise(logText) + '<br>';
            // auto scroll new entry unless scroll bar is not at bottom
            const bottom = 2 * baseFontSize;// 2 lines
            const pos = Math.abs(log.scrollHeight - log.clientHeight - log.scrollTop);
            if (pos < bottom) log.scrollTop = log.scrollHeight;
          }
        }

        function colorise(line) {
          // color message according to its type
          let colorVar = "";
          if (line.includes("WARN")) colorVar = "warnColor";
          if (line.includes("ERROR")) colorVar = "errColor";
          if (line.includes("DEBUG")) colorVar = "dbgColor";
          if (line.includes("CHECK")) colorVar = "chkColor";
          if (colorVar.length > 0) {
            const color = root.getPropertyValue('--' + colorVar);
            return "<b><font color=" + color + ">" + line + "</font></b>";
          } else return line;
        }
        
        function sendWsUpdates(doAction) {    
          // get each required update element and obtain id/name and value into array to send as json 
          let jarray = {};
          jarray["action"] = doAction;
          $$('.update-action').forEach(el => {
            if (el.nodeName == "INPUT") jarray[el.getAttribute('id')] = el.value.trim();
          });
          sendCmd('U' + JSON.stringify(jarray));
        }
        
        async function sendControl(key, value) {
          // send only  
          if (value != null) {
            const encodedValue = encodeURIComponent(value).replace(/#/g, '%23');
            const response = await fetch(encodeURI("/control?" + key + "=") + encodedValue);
            if (!response.ok) alert(response.status + ": " + response.statusText);
          }
        }
        
        async function sendControlResp(key, value) {
          // send and apply response
          const encodedValue = encodeURIComponent(value).replace(/#/g, '%23');
          const response = await fetch(encodeURI("/control?" + key + "=") + encodedValue);
          if (response.ok) {
            updateData = await response.json();
            updateStatus();
          } else alert(response.status + ": " + response.statusText);  
        }
        
        /*********** config functions ***********/
        
        async function getConfig(cfgGroup) {
          // request config json for selected group
          const response = await fetch('/status?123456789' + cfgGroup);
          if (response.ok) {
            const configData = await response.json();
            // format received json into html table
            buildTable(configData, cfgGroup);
          } else alert(response.status + ": " + response.statusText); 
        }
        
        function buildTable(configData, cfgGroup) {
          // dynamically build table of editable settings
          let divShowData = isDefined($('.config-group#Main'+cfgGroup)) ? $('.config-group#Main'+cfgGroup) : $('.config-group#Cfg');
          const retain = divShowData.id == 'Main'+cfgGroup ? true : false; // retain main page
          divShowData.innerHTML = "";
          if (cfgGroupNow != cfgGroup || retain) { // setup different config grouop
            cfgGroupNow = cfgGroup;
            const table = document.createElement("table"); 
            // Create table header row from heading names
            const colHeaders = ['Setting Name', 'Setting Value']; 
            let tr = table.insertRow(-1); 
            for (let i = 0; i < colHeaders.length; i++) {
              let th = document.createElement("th");    
              th.innerHTML = colHeaders[i];
              tr.appendChild(th);
            }

            // add each setting as a row containing setting label and setting value
            let nextPair = 3;
            let saveKey, saveVal;
            Object.entries(configData).forEach(([key, value]) => {
              if (key != "cfgGroup") { // skip over this entry 
                if (nextPair == 3) {
                  // new row
                  tr = table.insertRow(-1);
                  nextPair = 0;
                }
                if (nextPair == 0) {
                  // save key and value
                  saveKey = key;
                  saveVal = value;
                  nextPair = 1;
                } else if (nextPair == 1) {
                  // insert label for setting
                  tr.insertCell(-1).innerHTML = value; 
                  nextPair = 2;
                } else {
                  // get input field type and build html
                  let inputHtml;
                  let valCntr = 0;
                  switch (value.charAt(0)) {
                    case 'T': // text input
                      inputHtml = '<input type="text" class="configItem" id="' + saveKey + '" value="'+ saveVal +'" >';
                    break;
                    case 'N': // number input
                      inputHtml = '<input type="number" class="configItem" id="' + saveKey + '" value="'+ saveVal +'" >';
                    break;
                    case 'S': 
                      // drop down select
                      valCntr = 0;
                      inputHtml = '<select id="' + saveKey + '" class="selectField">';
                      value.substring(2).split(":").forEach(opt => {
                        inputHtml += '<option value="' + valCntr + '" ' + (saveVal == valCntr ? 'selected="selected"' : '') + '>' + opt + '</option>';
                        valCntr++;
                      });
                      inputHtml += '</select>';
                    break;
                    case 'C':
                      // format checkbox as slider
                      inputHtml = '<div class="switch"><input type="checkbox" class="configItem" id="' + saveKey;
                      inputHtml += '" value="'+ saveVal +'"' + (saveVal == 1 ? ' checked' : '') + '>';
                      inputHtml += '<label class="slider" for="' + saveKey + '"></label></div>';
                    break;
                    case 'D': // display only
                      inputHtml = '<input type="text" class="configItem" id="' + saveKey + '" value="'+ saveVal +'" readonly>';
                    break;
                    case 'R': // R:min:max:step
                      // format number as range slider 
                      const range = value.substring(2).split(":");
                      inputHtml = '<div class="input-group">';
                      inputHtml += '<input type="range" class="configItem" id="' + saveKey + '" min="' + range[0] + '" max="' + range[1];
                      inputHtml += '" step="' + range[2] + '" value="' + saveVal + '"><div name="rangeVal">' + saveVal + '</div></div>';
                    break;
                    case 'B': // B:lab1:lab2:etc
                      // radio button group
                      valCntr = 0;
                      inputHtml = '';
                      value.substring(2).split(":").forEach(opt => {
                        inputHtml += opt + '<input type="radio" class="configItem" name="' + saveKey + '" value="' + valCntr +
                          (saveVal == valCntr ? '" checked>' : '">');
                        valCntr++;
                      });
                    break;
                    case 'A': // action button
                      inputHtml = '<input type="button" class="configItem" id="' + saveKey + '" value="'+ saveVal +'" >';
                    break;
                    default:
                      alert("Unhandled config input type " + value);
                    break;
                  }
                  tr.insertCell(-1).innerHTML = inputHtml;
                  nextPair = 3;
                }
              }
            })
            // add the newly created table at placeholder
            divShowData.appendChild(table);
          } else cfgGroupNow = -1;
        }


       /************** Hub ****************/
            
      // create image elements with the saved IPs on page load
      async function createImageElements(ipAddresses) {
        const container = document.getElementById('imageContainer');
        container.innerHTML = ''; // Clear existing content

        // Convert the array of IP addresses into individual IPs
        for (const ip of ipAddresses) {
          // Create a container div for each image
          const ipContainer = document.createElement('div');
          ipContainer.classList.add('ipContainer');
          // Create an image element
          const hubImg = document.createElement('img');
          hubImg.classList.add('hubImg');
          // Set the source attribute to request image
          hubImg.src = `http://${ip}`;
          // Set an alt attribute for accessibility
          hubImg.alt = `No Image`;

          // Create a remove button for each container
          const removeButton = document.createElement('span');
          removeButton.classList.add('removeButton');
          removeButton.classList.add('iconSize');
          removeButton.innerHTML = '×';
              
          removeButton.onclick = function (event) {
            event.stopPropagation(); // Prevent container click from triggering at the same time
            // Remove the IP from local storage, remove the IP container, and update images
            const updatedIPs = removeFromLocalStorage(ip);
            container.removeChild(ipContainer);
            createImageElements(updatedIPs);
          };

          // Create a text element to display the IP address overlay
          const ipUrl = document.createElement('span');
          ipUrl.classList.add('ipUrl');
          ipUrl.textContent = ip;
          ipUrl.style.display = 'none';
          const ipText = document.createElement('span');
          ipText.classList.add('ipText');
          let ipStr = ip;
          const index = ip.indexOf('/');
          if (index !== -1) ipStr = ip.substring(0, index);
          ipText.textContent = ipStr;

          // Append the image, IP text, and remove button to the container
          ipContainer.appendChild(ipUrl);
          ipContainer.appendChild(ipText);
          ipContainer.appendChild(removeButton);
          ipContainer.appendChild(hubImg);
          removeButton.style.position = 'absolute';
          removeButton.style.top = '0';
          removeButton.style.right = '0';

          // Add click event listener to each container for fetching the web page for the IP address
          ipContainer.onclick = function () {
            window.open(`http://${ipStr}`, '_blank');
          };

          // Append the container to the main image container
          container.appendChild(ipContainer);
        }
      }

      function refreshAllContainers() {
        const containers = document.querySelectorAll('.ipContainer');
        containers.forEach((container) => {
          const ip = container.querySelector('.ipUrl').textContent;
          const hubImg = container.querySelector('img');
          hubImg.src = `http://${ip}`;
        });
      }

      // Function to create remote device image link when the "Add IP" button is clicked
      function addIP() {
        const ipInput = document.getElementById('ipInput');
        const ipAddresses = localStorage.getItem('enteredIPs') ? JSON.parse(localStorage.getItem('enteredIPs')) : [];

        // Add the entered IP to the array
        let newIP = ipInput.value.trim();
        if (newIP !== '' && !ipAddresses.includes(newIP)) {
          // if only ip address, add app specific URI
          // for any other app, enter full URL
          if (newIP.indexOf('/') == -1) newIP += appHub;
          ipAddresses.push(newIP);
          localStorage.setItem('enteredIPs', JSON.stringify(ipAddresses));
          // Call the function to create image elements with the updated IP addresses
          createImageElements(ipAddresses);
          // Clear the input field
          ipInput.value = '';
        }
      }

      // Function to remove an IP from local storage
      function removeFromLocalStorage(ip) {
        const ipAddresses = localStorage.getItem('enteredIPs') ? JSON.parse(localStorage.getItem('enteredIPs')) : [];
        const updatedIPs = ipAddresses.filter(existingIP => existingIP !== ip);
        // Update local storage with the modified array
        localStorage.setItem('enteredIPs', JSON.stringify(updatedIPs));
        return updatedIPs;
      }

      // Function to clear local storage
      function clearLocalStorage() {
        const ipAddresses = localStorage.getItem('enteredIPs') ? JSON.parse(localStorage.getItem('enteredIPs')) : [];
        localStorage.removeItem('enteredIPs');
        createImageElements(ipAddresses); // Update images after clearing local storage
      }

      // Retrieve and populate the input field with the saved IPs on page load
      let hubObserver = new IntersectionObserver (entries => {
        // refresh hub when becomes visible
        entries.forEach(entry => { if (entry.isIntersecting) {
          const savedIPs = localStorage.getItem('enteredIPs');
          const ipAddresses = savedIPs ? JSON.parse(savedIPs) : [];
          createImageElements(ipAddresses);
        }});
      }); 
      let deviceHubEl = document.getElementById('DeviceHub');
      if (deviceHubEl) hubObserver.observe(deviceHubEl);
      
      
      /*********************** Browser Mic *********************/
      
      // Windows needs to allow microphone use in Microphone Privacy Settings
      //
      // In Microphone Properties / Advanced, check bit depth and sample rate (normally 16 bit 48kHz)
      //
      // chrome needs to allow access to mic from insecure (http) site:
      // Go to : chrome://flags/#unsafely-treat-insecure-origin-as-secure
      // Enter following URL in box: http://<app_ip_address>
      
      let micStream;
      let isMicStreaming = false;
      const inSampleRate = 48000;
      let outSampleRate = 16000;
      let Resample;
      
      function createAudioWorkletScript(sampleRateRatio) {
        return `
          class Resample extends AudioWorkletProcessor {
            constructor() {
              super();
              this.sampleRateRatio = ${sampleRateRatio};
              this.port.onmessage = this.handleMessage.bind(this);
            }
            
            handleMessage(event) {
              if (event.data.type === 'stop') {
                this.port.close(); // Close the worklet port for future messages
                return;
              }
            }

            resampleAudio(inputChannel) {
              // resample 16 bit 46kHz to 16kHz
              const outputLength = Math.round(inputChannel.length / this.sampleRateRatio);
              const resampledData = new Int16Array(outputLength);
              let outputIndex = 0;
              for (let i = 0; i < outputLength; i++) {
                const inputIndex = Math.round(i * this.sampleRateRatio);
                // Clamp the input index to avoid potential out-of-bounds access
                const clampedIndex = Math.min(inputIndex, inputChannel.length - 1);
                // convert float values -1 : 1 to 16 bit integers
                resampledData[outputIndex++] = inputChannel[clampedIndex] * 32767;
              }
              return resampledData;
            }

            process(inputs, outputs, parameters) {
              const inputChannel = inputs[0][0];
              if (!inputChannel || !inputChannel.length) return true; // empty data
              
              const resampledData = this.resampleAudio(inputChannel);
              this.port.postMessage(resampledData);
              return true;
            }
          }
          registerProcessor("resample", Resample);
        `;
      }

      async function runMic() {
        // start mic
        const sampleRateRatio = inSampleRate / outSampleRate;
        const audioWorkletScript = createAudioWorkletScript(sampleRateRatio);
        try {
          micStream = await navigator.mediaDevices.getUserMedia({ audio: true });
          if (!ws) initWebSocket();
          const context = new AudioContext();
          const source = context.createMediaStreamSource(micStream);
          await context.audioWorklet.addModule('data:text/javascript;base64,' + btoa(audioWorkletScript));
          Resample = new AudioWorkletNode(context, "resample");;
          source.connect(Resample).connect(context.destination);

          if (ws) {
            if (ws.readyState === WebSocket.OPEN) {
              isMicStreaming = true;
              Resample.port.onmessage = function(event) {
                ws ? ws.send(event.data) : closeMic(); // Send the audio chunk 
              };
            }
          }
        } catch (error) {
          alert("Chrome needs security exception for " + baseHost);
        }
      }

      function closeMic() {
        // stop streaming
        isMicStreaming = false;
        if (ws) ws.send('X');
        // close down mic
        if (micStream) {
          micStream.getTracks().forEach(track => track.stop()); // Close the microphone stream
          micStream = null;
        }
        if (Resample && Resample.port) Resample.port.postMessage({ type: 'stop' }); // Send stop message
        if (Resample) Resample.disconnect();
        try { micAction(false); } 
        catch (error) {}
      }
      
      function micRemState(value) {
        value ? runMic() : closeMic();
      }