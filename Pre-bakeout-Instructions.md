
1.  Open COMM port, verify connection. Check all Comm Ports until right one is found based off acknowledgements
    
2.  Set resolution to low, start streaming
    
3.  Instruct the user to:
    

1.  Find a physical area at least 230cm long, with a flat, level surface. Test with a level
    
2.  Ensure there are no variable lighting conditions so that lighting stays constant. This area should be available for the post-bakeout test as well so that lighting conditions remain the same for both tests. Ideally a room with no windows.
    
3.  Position the ArduCam so that the direction the lens face is parallel to the ground (not facing the sky or ground).
    
4.  Place the target 191.5cm (measured from the end of the lens face) away from the camera
    
5.  Look on the Arducam Host display menu and ensure that the entirety of the target image is in frame and the image also takes up the entire frame.
    
6.  Once the positioning of the Arducam is established where the target frame is appropriately in frame (Image taking up the entire frame), secure Arducam and wires to their position with tape.
    
7.  If there are qualitative variable lighting conditions such as an open window with natural lighting, record the lighting conditions down so that it may be as closely replicated as possible post bakeout.
    
8.    
    

5.  Stop streaming
    
6.  Convert the image to gray, calculate the average brightness of the image, use a binary search to adjust the exposure, then take an image until the exposure is 160 plus/minus 5
    
7.  Print the exposure value to the menu and tell user to record it
    
8.  Change the resolution to “2592x1944”
    
9.  Create a folder “Pre-Bakeout”
    
10.  Take five images, each named (“Pre-Bakeout-” + (angle)+”-”imageNumber) Where angle is the current angle. Starting at 0. Image number is the image taken at that angle, so the first 3 images taken should be “Pre-Bakeout-0-1”, “Pre-Bakeout-0-2”, “Pre-Bakeout-0-3”
    
11.  Instruct the user to rotate the test apparatus by 15 degrees
    
12.  Update the angle variable to reflect this
    
13.  Repeat steps 9 and 10 until the full 360 degrees has been completed, this should result in 24 tests
    
14.  In between each of these user instructions there should be a “next” button upon completion, as well as a “back” button to revert the test back a step
