function Oscilloscope(canvas) {
	this.canvas=canvas;
	if(window.devicePixelRatio==null)this.pr=1;
	else this.pr=window.devicePixelRatio;
	
	// bounding rect of the waveform area
	this.waveformAreaX = 0;
	this.waveformAreaY = 0;
	this.waveformAreaW = 0;
	this.waveformAreaH = 0;

	this.colors = ["#0000FF", "#FF0000", "#00FF00"];

	this.dataIsMipmap = false;
	this.data = [];

	// the x extents when fully zoomed out
	this.xLower = 0;
	this.xUpper = 100;

	// the virtual position and scale of the data array
	this.dataLeft = 0;
	this.dataWidth = 1; // virtual width of one data point

	// the y extents when fully zoomed out
	this.yLower = -1;
	this.yUpper = 1;

	// zoom parameters
	this.zoomPosX = 0.5;
	this.zoomPosY = 0.;
	this.zoomScaleX = 1.;
	this.zoomScaleY = 1.;
	this.selectionRect = null; // [x,y,x2,y2]
	this.zoomStack = [];
	this.d=false;
	this.last_i=-1;
	this.last_v=-1;
	
	canvas.onselectstart=function(){return false;};
	canvas.style.cursor="default";
	
	this.nSamples = function() {
		var arr = this.channels ? this.channels[0] : this.data;
		return this.dataIsMipmap ? (arr.length/2) : arr.length;
	};
	// returns [i1, i2], the x (between xLower and xUpper) range of the data that is visible
	this.zoomDataExtents = function() {
		var x1 = this.zoomPosX - 1./this.zoomScaleX/2;
		var x2 = this.zoomPosX + 1./this.zoomScaleX/2;
		var len = (this.xUpper - this.xLower) - 1;
		return [x1*len, x2*len];
	};
	// returns [start, end], the range of visible data scaled to [0, 1]
	this.zoomVirtualExtents = function() {
		var x1 = this.zoomPosX - 1./this.zoomScaleX/2;
		var x2 = this.zoomPosX + 1./this.zoomScaleX/2;
		return [x1, x2];
	}
	// returns [lower, upper], the numeric range of visible data
	this.zoomYExtents = function() {
		var y1 = this.zoomPosY - 1./this.zoomScaleY/2;
		var y2 = this.zoomPosY + 1./this.zoomScaleY/2;
		var yCenter = (this.yUpper + this.yLower) / 2;
		var yScale = (this.yUpper - this.yLower);
		return [y1 * yScale + yCenter,
				y2 * yScale + yCenter];
	};
	// returns [lower, upper], the numeric range of visible data
	this.zoomYVirtualExtents = function() {
		var y1 = this.zoomPosY - 1./this.zoomScaleY/2;
		var y2 = this.zoomPosY + 1./this.zoomScaleY/2;
		return [y1, y2];
	};
	// given x coordinate within canvas, return data x (between xLower and xUpper)
	this.xToIndex = function(canvasX) {
		var x = canvasX - this.waveformAreaX;
		var ext = this.zoomDataExtents();
		x = (x / this.waveformAreaW);
		return ext[0] + (ext[1] - ext[0]) * x;
	};
	// given x coordinate within canvas, return data x scaled to [0, 1]
	this.xToVirtual = function(canvasX) {
		var x = canvasX - this.waveformAreaX;
		var ext = this.zoomVirtualExtents();
		x = (x / this.waveformAreaW);
		return ext[0] + (ext[1] - ext[0]) * x;
	};
	// given y coordinate within canvas, return y value
	this.yToValue = function(canvasY) {
		var y = canvasY - this.waveformAreaY;
		var ext = this.zoomYExtents();
		y = 1.0 - (y / this.waveformAreaH);
		return ext[0] + (ext[1] - ext[0]) * y;
	};
	
	// return an array of the lower/upper bound
	// of each x pixel
	this.getPoints=function(data, width) {
		var lower = [];
		var upper = [];
		lower[width-1] = 0;
		upper[width-1] = 0;

		// total number of data points if the array was extended
		// to xLower and xUpper
		var len = (this.xUpper - this.xLower)/this.dataWidth;
		// how many data points per pixel
		var scale = len/width/this.zoomScaleX;
		// datapoint index of pixel 0
		var offset = (this.zoomPosX - 0.5/this.zoomScaleX)*len;
		// start index of the array
		var leftLimit = this.dataLeft / this.dataWidth;
		// end index of the array
		var rightLimit = leftLimit + this.nSamples();
		for(var i=0; i<width; i++) {
			var leftBound = Math.round(i*scale + offset);
			var rightBound = Math.round((i+1)*scale + offset);
			if(rightBound < leftLimit || leftBound >= rightLimit) {
				// out of bounds point
				lower[i] = 1;
				upper[i] = -1;
				continue;
			}
			if(leftBound < leftLimit) leftBound = leftLimit;
			if(rightBound >= rightLimit) rightBound = rightLimit-1;
			var lowerBound = 1e12, upperBound = -1e12;
			if(this.dataIsMipmap) {
				for(var x=leftBound-leftLimit; x<=rightBound-leftLimit; x++) {
					var valL = data[x*2], valU = data[x*2+1];
					if(valL < lowerBound) lowerBound = valL;
					if(valU > upperBound) upperBound = valU;
				}
			} else {
				for(var x=leftBound-leftLimit; x<=rightBound-leftLimit; x++) {
					var val = data[x];
					if(val < lowerBound) lowerBound = val;
					if(val > upperBound) upperBound = val;
				}
			}
			lower[i] = lowerBound;
			upper[i] = upperBound;
		}
		return [lower, upper];
	}
	this.updatePoints = function() {
		if(this.channels) {
			this.points = new Array(this.channels.length);
			for(var i=0; i<this.channels.length; i++)
				this.points[i] = this.getPoints(this.channels[i], this.waveformAreaW);
		} else {
			this.points = [this.getPoints(this.waveformAreaW)];
		}
	};
	this.drawPointsDense = function(gc, x1, x2) {
		for(var ch=0; ch<this.points.length; ch++) {
			var points = this.points[ch];
			var offsetY = this.zoomPosY*this.zoomScaleY;
			var yScale = this.zoomScaleY/(this.yUpper - this.yLower);
			var yCenter = (this.yUpper + this.yLower) / 2;
			var w=this.waveformAreaW;
			var h=this.waveformAreaH;
			gc.fillStyle = this.colors[ch];
			for(var x=x1;x<=x2;x++) {
				var p0 = points[0][x] - yCenter;
				var p1 = points[1][x] - yCenter;
				if(p0 > p1) continue;
				var y1=Math.ceil((0.5-p0*yScale + offsetY)*h + 0.5);
				var y2=Math.floor((0.5-p1*yScale + offsetY)*h - 0.5);
				
				gc.fillRect(x,y2,1,y1-y2);
			}
		}
	};
	this.drawPointsSparse = function(gc, x1, x2) {
		var channels = this.channels ? this.channels : [this.data];
		for(var ch=0; ch<channels.length; ch++) {
			var color = this.colors[ch];
			var data = channels[ch];
			gc.fillStyle = color;
			gc.strokeStyle = color;
			gc.beginPath();
			var offsetY = this.zoomPosY*this.zoomScaleY;
			var yScale = this.zoomScaleY/(this.yUpper - this.yLower);
			var yCenter = (this.yUpper + this.yLower) / 2;
			var w=this.waveformAreaW;
			var h=this.waveformAreaH;
			// total number of data points if the array was extended
			// to xLower and xUpper
			var len = (this.xUpper - this.xLower)/this.dataWidth;
			// how many pixels per data point
			var scale = w*this.zoomScaleX/len;
			// start index of the array
			var leftLimit = this.dataLeft / this.dataWidth;
			// end index of the array
			var rightLimit = leftLimit + this.nSamples();
			var ext = this.zoomDataExtents();
			var xBegin = Math.floor(ext[0]), xEnd = Math.ceil(ext[1])+1;
			xBegin = Math.max(xBegin, leftLimit);
			xEnd = Math.min(xEnd, rightLimit);
			
			for(var i=xBegin; i<xEnd; i++) {
				var x = (i-ext[0])/len*this.zoomScaleX*w;
				var p0 = data[i - leftLimit] - yCenter;
				var y = Math.round((0.5-p0*yScale + offsetY)*h);
				if(i == xBegin)
					gc.moveTo(x,y);
				else gc.lineTo(x,y);
			}
			gc.stroke();
			gc.closePath();
		}
	};
	this.doDraw=function(ctx,xBegin,xEnd) {
		ctx.lineWidth = 2;
		
		var x1,y1;
		var w=this.waveformAreaW;
		var h=this.waveformAreaH;
		
		x1=(xBegin==null?0:xBegin);
		x2=(xEnd==null?w-1:xEnd);
		
		var gc=ctx;
		ctx.fillStyle="#FFFFFF";
		ctx.clearRect(0,0,gc.canvas.width,gc.canvas.height);
		
		gc.setTransform(1,0,0,1,this.waveformAreaX, this.waveformAreaY);
		var ext = this.zoomDataExtents();
		if(ext[1] - ext[0] > w || this.dataIsMipmap)
			this.drawPointsDense(gc,x1,x2);
		else this.drawPointsSparse(gc,x1,x2);
		
		//gc.stroke();
		if(this.selectionRect) {
			gc.beginPath();
			gc.fillStyle = "rgba(0.5,0.2,0.2,0.2)";
			gc.strokeStyle = "#777777";
			var x1 = this.selectionRect[0], y1 = this.selectionRect[1];
			gc.rect(x1, y1,
					this.selectionRect[2]-x1, this.selectionRect[3]-y1);
			gc.fill();
			gc.stroke();
			gc.closePath();
		}
	};
	
	canvas.__oscilloscope=this;
	this.redraw=function(x1,x2) {
		this.doDraw(this.canvas.getContext("2d"),x1,x2);
	};
	this.refresh=function() {
		//alert(this.canvas.clientWidth);
		this.canvas.width=this.canvas.clientWidth*this.pr;
		this.canvas.height=this.canvas.clientHeight*this.pr;
		this.waveformAreaX = 10;
		this.waveformAreaY = 0;
		this.waveformAreaW = this.canvas.width-20;
		this.waveformAreaH = this.canvas.height;
		
		//alert(this.canvas.clientWidth+" "+this.canvas.clientHeight);
		this.updatePoints();
		this.redraw();
	};
	
	
	// zoom
	this.handleMouseDown=function(ev) {
		if(ev.button!=0) return;
		var e=ev?ev:event;
		var eX=e.clientX-this.canvas.getBoundingClientRect().left;
		var eY=e.clientY-this.canvas.getBoundingClientRect().top;
		eX *= this.pr; eY *= this.pr;
		this.mouseDownX = eX - this.waveformAreaX;
		this.mouseDownY = eY - this.waveformAreaY;
		this.mouseIsDown = true;
		//this.selectionRect = [0,0,0,0];
		return true;
	}
	this.handleMouseMove=function(ev) {
		var e=ev?ev:event;
		if((e.buttons & 1) == 0) return true;
		if(!this.mouseIsDown) return true;
		var eX=e.clientX-this.canvas.getBoundingClientRect().left;
		var eY=e.clientY-this.canvas.getBoundingClientRect().top;
		eX *= this.pr; eY *= this.pr;
		eX -= this.waveformAreaX;
		eY -= this.waveformAreaY;
		if(eY<0) eY=0;
		if(eX<0) eX=0;
		if(eX>this.waveformAreaW) eX = this.waveformAreaW;
		if(eY>this.waveformAreaH) eY = this.waveformAreaH;
		
		this.selectionRect = [this.mouseDownX, this.mouseDownY, eX, eY];
		//alert(JSON.stringify(this.selectionRect));
		this.redraw();
		return true;
	}
	this.handleMouseUp=function(ev) {
		var e=ev?ev:event;
		if(!this.mouseIsDown) return true;
		
		var w=this.waveformAreaW, h=this.waveformAreaH;
		var x1 = Math.min(this.selectionRect[0], this.selectionRect[2]);
		var y1 = h - Math.max(this.selectionRect[1], this.selectionRect[3]);
		var x2 = Math.max(this.selectionRect[0], this.selectionRect[2]);
		var y2 = h - Math.min(this.selectionRect[1], this.selectionRect[3]);
		
		this.selectionRect = null;
		this.mouseIsDown = false;
		
		if(y2-y1 < 10 && x2-x1 < 10) {
			this.refresh();
			return true;
		}
		
		// update zoom
		if(this.zoomStack.length < 3)
			this.zoomStack.push([this.zoomPosX, this.zoomPosY, this.zoomScaleX, this.zoomScaleY]);
		
		// center of the selection rect, normalized to [0,1]
		var centerX = (x1+x2)/2./w;
		var centerY = (y1+y2)/2./h;
		var extentX = (x2-x1)/w;
		var extentY = (y2-y1)/h;
		// old bounds of zoom, normalized to [0,1]
		var oldX1 = this.zoomPosX - 1./this.zoomScaleX/2.;
		var oldY1 = this.zoomPosY - 1./this.zoomScaleY/2.;
		// new bounds of zoom, normalized to [0,1]
		var newCenterX = oldX1 + centerX/this.zoomScaleX;
		var newCenterY = oldY1 + centerY/this.zoomScaleY;
		this.zoomPosX = newCenterX;
		this.zoomScaleX /= extentX;
		this.zoomPosY = newCenterY;
		this.zoomScaleY /= extentY;
		//alert(newCenterY);
		
		if(this.onzoom) this.onzoom();
		this.refresh();
		return true;
	}
	this.zoomOut = function() {
		if(this.zoomStack.length == 0) return;
		var tmp = this.zoomStack.pop();
		this.zoomPosX = tmp[0];
		this.zoomPosY = tmp[1];
		this.zoomScaleX = tmp[2];
		this.zoomScaleY = tmp[3];
		if(this.onzoom) this.onzoom();
		this.refresh();
	};
	this.canvas.onmousedown = function(ev) {
		return this.__oscilloscope.handleMouseDown(ev);
	};
	this.canvas.oncontextmenu = function(ev) {
		this.__oscilloscope.zoomOut();
		return false;
	};
	this.canvas.ontouchstart=function(event) {
		var ev=new Object();
		var touch = event.targetTouches[0];
		ev.clientX=touch.pageX;
		ev.clientY=touch.pageY;
		ev.button = 0;
		if(event.targetTouches.length == 2) {
			if(this.selectionRect != null) return;
			this.__oscilloscope.zoomOut();
		}
		if(event.targetTouches.length == 1) {
			this.__oscilloscope.handleMouseDown(ev);
		}
		event.preventDefault();
		return false;
	};
	this.canvas.ontouchend=function(ev) {
		return this.__oscilloscope.handleMouseUp(ev);
	};
	this.canvas.ontouchmove=function(event){
		if (event.targetTouches.length >= 1) {
			var ev=new Object();
			var touch = event.targetTouches[0];
			ev.clientX=touch.pageX;
			ev.clientY=touch.pageY;
			ev.buttons = 1;
			this.__oscilloscope.handleMouseMove(ev);
			event.preventDefault();
			//alert(ev.clientY);
		}
	};
	/*this.canvas.onmousemove = function(ev) {
		return this.__oscilloscope.handleMouseMove(ev);
	};
	this.canvas.onmouseup = function(ev) {
		return this.__oscilloscope.handleMouseUp(ev);
	};*/
	
	var __oscilloscope = this;
	window.addEventListener('mousemove', function(ev) {
		return __oscilloscope.handleMouseMove(ev);
	}, false);
	window.addEventListener('mouseup', function(ev) {
		return __oscilloscope.handleMouseUp(ev);
	}, false);
	
}
