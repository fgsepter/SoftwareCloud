## Login and Home Page
1. <span style="color:red">login request</span>:
http://127.0.0.1:3000/UI_interface/login?username=123&password=234
- success: redirect to homePage
- fail: loginError Page

- frontend recv (username, pwd)
- send to master: get(username, "password") -> return string
    frontend: pwd == get(username, "password")

2. register request:
http://127.0.0.1:3000/UI_interface/register?username=123&password=234
- success: add new user, redirect to homePage
- fail: registerError Page

- frontend recv (username, pwd)
- to master: put(username, "password", pwd)

3. <span style="color:red">webmail</span>:
http://127.0.0.1:3000/UI_interface/webmail
- redirect to webmail, load inbox of current user
should be able to delete the message, write a reply, or forward it to another address

- if not SMTP:
    - frontend recv (sender, receiver, subject, content)
    - to master: 
    - master to backend: put(username, "emailID", (sender, receiver, subject, content))
        -> return "mail1:"

4. <span style="color:red">storage</span>:
http://127.0.0.1:3000/UI_interface/storage
- redirect to storage, load directories and files of current user

- 

5. <span style="color:red">admin</span>:
http://127.0.0.1:3000/UI_interface/console
- redirect to admin page, load server tables

6. change password:
http://127.0.0.1:3000/UI_interface/changePassword?password=123
- success: update password, redirect to homePage
- fail: changePasswordError page

7. logout:
http://127.0.0.1:3000/UI_interface/logout
- redirect to homePage

## Emails
8. <span style="color:red">write email</span>:
http://127.0.0.1:3000/UI_interface/composeEmail?from=1&to=2&subject=123&content=234
- write email

9. <span style="color:red">open email</span>:
http://127.0.0.1:3000/UI_interface/openEmail?id=xxx
- load detailed email information, redirect to single email page

10. <span style="color:red">delete email</span>:
http://127.0.0.1:3000/UI_interface/deleteEmail?id=xxx
- delete email, update inbox of the current user, redirect to webmail page

11. <span style="color:yellow">reply email</span>:
http://127.0.0.1:3000/UI_interface/replyEmail?id=xxx&&subject=123&content=234
- similiar to 8

12. <span style="color:yellow">forward email</span>:
http://127.0.0.1:3000/UI_interface/forwardEmail?id=xxx&to=2
- similiar to 8


## Files
13. <span style="color:red">upload file</span>:
http://127.0.0.1:3000/UI_interface/uploadFile?file=lecture10.pptx
- add file, update storage page

14. <span style="color:red">download file</span>:
http://127.0.0.1:3000/UI_interface/downloadFile?file=lecture10.pptx
- download file to local

## Admin