const char* OTApage = R"~(
<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>
<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>
  <input type='file' name='update'>
  <input type='submit' value='Update'>
</form>
<div id='prg'>progress: 0%</div>
<script>
  var baseHost = document.location.origin
  var otaUrl = baseHost + '/update'
  $('form').submit(function(e){
  e.preventDefault();
  var form = $('#upload_form')[0];
  var data = new FormData(form); 
  $.ajax({
    url: otaUrl,
    type: 'POST',
    data: data,
    contentType: false,
    processData: false,
    xhr: function() {
      var xhr = new window.XMLHttpRequest();
      xhr.upload.addEventListener('progress', function(evt) {
        if (evt.lengthComputable) {
          var per = evt.loaded / evt.total;
          $('#prg').html('progress: ' + Math.round(per*100) + '%');
        }
      }, false);
      return xhr;
    }
   });
  });
</script>
)~";
