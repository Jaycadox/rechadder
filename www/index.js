function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}
  
const cd = document.getElementById("codeblock")
const btn = document.getElementById("hellobtn")
const inp = document.getElementById("inpu")
const scripts = document.getElementById("script_list")
const reload_scripts = document.getElementById("rldscripts");
const cnt_btn = document.getElementById("ctnbtn");
const ip = document.getElementById("ip");
const port = document.getElementById("port");
const username = document.getElementById("username");
var textarea = document.getElementById('codeblock');
ip.value = "loading...";
username.value = "loading...";
port.value = "loading...";
(async () => {
    const rawResponse = await fetch("http://localhost:8080/internal/session_details/", {
        method: 'GET', // *GET, POST, PUT, DELETE, etc.
        mode: 'cors', // no-cors, *cors, same-origin
        cache: 'no-cache', // *default, no-cache, reload, force-cache, only-if-cached
        credentials: 'same-origin', // include, *same-origin, omit
        redirect: 'follow', // manual, *follow, error
        referrerPolicy: 'no-referrer', // no-referrer, *no-referrer-when-downgrade, origin, origin-when-cross-origin, same-origin, strict-origin, strict-origin-when-cross-origin, unsafe-url
    });
    const content = await rawResponse.json();
    ip.value = content.ip;
    username.value = content.username;
    port.value = content.port;
})();
function get_chat_history()
{
    return fetch("http://localhost:8080/internal/message_history/", {
        method: 'GET', // *GET, POST, PUT, DELETE, etc.
        mode: 'cors', // no-cors, *cors, same-origin
        cache: 'no-cache', // *default, no-cache, reload, force-cache, only-if-cached
        credentials: 'same-origin', // include, *same-origin, omit
        redirect: 'follow', // manual, *follow, error
        referrerPolicy: 'no-referrer', // no-referrer, *no-referrer-when-downgrade, origin, origin-when-cross-origin, same-origin, strict-origin, strict-origin-when-cross-origin, unsafe-url
      });
}
let first = 5;
async function update_chat_loop()
{
    while(true)
    {
        const to_bottom = Math.abs(textarea.scrollTop - textarea.scrollHeight) < 600;
        get_chat_history().then(response=>response.text()).then(data=>{ 
            cd.innerHTML = data;
        })
        if(to_bottom || first > 0)
        {
            textarea.scrollTop = textarea.scrollHeight;
            first--;
        }

        await sleep(200);
    }
}
async function update_script_loop()
{
    while(true)
    {
        fetch("http://localhost:8080/internal/session/", {
            method: 'GET', // *GET, POST, PUT, DELETE, etc.
            mode: 'cors', // no-cors, *cors, same-origin
            cache: 'no-cache', // *default, no-cache, reload, force-cache, only-if-cached
            credentials: 'same-origin', // include, *same-origin, omit
            redirect: 'follow', // manual, *follow, error
            referrerPolicy: 'no-referrer', // no-referrer, *no-referrer-when-downgrade, origin, origin-when-cross-origin, same-origin, strict-origin, strict-origin-when-cross-origin, unsafe-url
        }).then(response => response.json()).then(data => {
            scripts.innerHTML = '';
            data.scripts.forEach(name => {
                let temp = document.createElement('input');
                temp.setAttribute("class", "inp full script");
                temp.setAttribute("disabled", true);
                temp.setAttribute("placeholder", name);
                temp.setAttribute("type", "text");
                scripts.appendChild(temp);
            });
        })
        await sleep(500);
    }
}
btn.onclick = (e) => {
    e.preventDefault();
    fetch("http://localhost:8080/internal/send_message/" + inp.value, {
        method: 'GET', // *GET, POST, PUT, DELETE, etc.
        mode: 'cors', // no-cors, *cors, same-origin
        cache: 'no-cache', // *default, no-cache, reload, force-cache, only-if-cached
        credentials: 'same-origin', // include, *same-origin, omit
        redirect: 'follow', // manual, *follow, error
        referrerPolicy: 'no-referrer', // no-referrer, *no-referrer-when-downgrade, origin, origin-when-cross-origin, same-origin, strict-origin, strict-origin-when-cross-origin, unsafe-url
      })
    inp.value = "";
};
cnt_btn.onclick = (e) => {
    e.preventDefault();
    fetch("http://localhost:8080/internal/connect/", {
        method: 'POST', // *GET, POST, PUT, DELETE, etc.
        mode: 'cors', // no-cors, *cors, same-origin
        cache: 'no-cache', // *default, no-cache, reload, force-cache, only-if-cached
        credentials: 'same-origin', // include, *same-origin, omit
        redirect: 'follow', // manual, *follow, error
        referrerPolicy: 'no-referrer', // no-referrer, *no-referrer-when-downgrade, origin, origin-when-cross-origin, same-origin, strict-origin, strict-origin-when-cross-origin, unsafe-url
        headers: {
            'Accept': 'application/json',
            'Content-Type': 'application/json'
        },
        body: JSON.stringify({
            ip: ip.value,
            port: port.value,
            username: username.value
        })
    })
    inp.value = "";
};
reload_scripts.onclick = (e) => {
    e.preventDefault();
    fetch("http://localhost:8080/internal/reload_scripts/", {
        method: 'GET', // *GET, POST, PUT, DELETE, etc.
        mode: 'cors', // no-cors, *cors, same-origin
        cache: 'no-cache', // *default, no-cache, reload, force-cache, only-if-cached
        credentials: 'same-origin', // include, *same-origin, omit
        redirect: 'follow', // manual, *follow, error
        referrerPolicy: 'no-referrer', // no-referrer, *no-referrer-when-downgrade, origin, origin-when-cross-origin, same-origin, strict-origin, strict-origin-when-cross-origin, unsafe-url
      })
};
inp.onkeydown = (e) => {
    if(e.key === 'Enter') {
        btn.click();
    }
}
update_chat_loop();
update_script_loop();