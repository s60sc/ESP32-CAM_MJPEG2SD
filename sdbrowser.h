
const char* sdBrowserHead = R"~(
<HTML>
  <head>
    <meta http-equiv="Content-Type" content="text/html; charset=utf-8" />
    <title>MJPEG streaming selection</title>
    <script src="https://ajax.googleapis.com/ajax/libs/jquery/2.1.3/jquery.min.js"></script>
    <style>
      body * {font-size: 18px;}
      input[type="checkbox"] {
        width : 20px;
        height : 20px;
      }
    </style>
  </head>    
  <body>
    </br>
    <h2>Recording params:</h2> 
    <table>    
)~";

const char* sdBrowserMid = R"~(
      <tr><td></td><td><input type="submit" id="submitButton" value="Submit"/></td></tr>
    </table></br>
    <h2>Replay selection:</h2>
    <select id="Folder">
      <option value="None">-- Select --</option>   
)~";

const char* sdBrowserTail = R"~(
   <select>
    <script type="text/javascript">   
      $(document).ready(function(){
        $(document).on("click", "#Folder", function() {
          $.ajax({
            url: "http://"+$(location).attr("host")+"/sd",
            data: {"folder": $('#Folder').val() },   
            success: function(response) { 
              // overwrite existing with supplied page html 
              var newDoc = document.open("text/html", "replace");
              newDoc.write(response);
              newDoc.close();
            }
          });
        });
      });
      $(document).ready(function(){
        $(document).on("click", '#submitButton', function(){
          $.ajax({
            url: "http://"+$(location).attr("host")+"/sd",
            data: {
              "frameRate": $('#frameRate').val(),
              "minFrames": $('#minFrames').val(),
              "debug": $('#debug').is(":checked") ? "1" : "0"
            },   
            success: function(response) { 
              // overwrite existing with supplied page html 
              var newDoc = document.open("text/html", "replace");
              newDoc.write(response);
              newDoc.close();
            }
          });
        });
      });
    </script>
  </body>
</HTML>
)~";
