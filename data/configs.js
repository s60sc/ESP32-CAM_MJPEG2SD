      
      async function getConfig(cfgGroup) {
        try {
          // request config json for selected group
          const response = await fetch('/status?123456789' + cfgGroup);
          if (!response.ok) {
            throw new Error(`Error: ${response.status}`);
          }
          const jsonData = await response.json();
          // format received json into table
          buildTable(jsonData);
        } catch (err) {
          console.log(err);
        }
      }
      
      function buildTable(jsonData) {
        // dynamically build table of editable settings
        const table = document.createElement("table"); 

        // Create table header row from heading names
        const colHeaders = ['Setting Name', 'Setting Value']; 
        let tr = table.insertRow(-1); 
        for (let i = 0; i < colHeaders.length; i++) {
          let th = document.createElement("th");    
          th.innerHTML = colHeaders[i];
          tr.appendChild(th);
        }

        // add each setting as a row containg setting name and setting value
        let newRow = 1;
        Object.keys(jsonData).forEach( function(key) {
          if (newRow) tr = table.insertRow(-1);
          let tabCell = tr.insertCell(-1);
          if (newRow) {
            tabCell.innerHTML = jsonData[key]; // name of setting
            newRow = 0;
          } else {
            // input field for value of setting
            tabCell.innerHTML = '<input type="text" name="' + key + '" value="' + jsonData[key] +'">'; 
            newRow = 1;
          }
        })

        // add the newly created table at placeholder
        const divShowData = document.getElementById('configTable');
        divShowData.innerHTML = "";
        divShowData.appendChild(table);
        
        // add event liatener on each input field
        document
         .querySelectorAll('input')
         .forEach(el => {
           el.onchange = () => updateConfig(el); // send updated input field value to device
        });
      }
      
      async function updateConfig (el) {
        // send updated config value to device
        try {
          const update = '/control?' + el.name + '=' + el.value;
          const encoded = encodeURI(update);
          const response = await fetch(update);
          if (!response.ok) {
            throw new Error(`Error: ${response.status}`);
          }
        } catch (err) {
          console.log(err);
        }
      }